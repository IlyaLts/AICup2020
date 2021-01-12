#include "MyStrategy.hpp"
#include <iostream>
#include <cmath>

#define MAP_SIZE 80

using namespace std;

// Building
const int buildingIndent = 2;
const int buildingIndentWithFog = 1;
const int numberOfBuildersForRepair = 5;
// Builder units
const int builderAttackBuilderDistance = 4;
const int buildersRunAwayDistance = 7;
const int builderRepairDistance = 20;
// Melee/Ranged units
const int allyRunAwayRange = 3;
const int enemyRunAwayRange = 10;
const int troopsAttackBuilderDistance = 5;
const int troopsAttackBaseDistance = 5;
// Don't run away from specific enemies when the distance is equal or less
const int ranged_dontRunAwayFromRanged = 5;
const int ranged_dontRunAwayFromMelee = 3;
const int melee_dontRunAwayFromRanged = 5;
const int melee_dontRunAwayFromMelee = 1;
// Is it worth to attack coefficients
const float enemyMeleeRunAwayMultiplier = 0.5f;
const float enemyRangedRunAwayMultiplier = 1.5f;
const float allyMeleeRunAwayMultiplier = 0.33f;
const float allyRangedRunAwayMultiplier = 1.0f;
// Etc
const float troopsBuildersRatio = 0.4f;

vector<Vec2Int> knownEnemies;
vector<Vec2Int> knownEnemySpawns;

int unitPositionsAtLastTick[MAP_SIZE][MAP_SIZE];
int unitPositionsAtCurrentTick[MAP_SIZE][MAP_SIZE];

enum tile_t
{
    TILE_EMPTY,
    TILE_BLOCKED,
    TILE_DESTROYABLE
};

enum path_t
{
    PATH_EMPTY = -5,
    PATH_DESTROYABLE = -4,
    PATH_BLOCKED = -3,
    PATH_TARGET_FOUND = -2,
    PATH_TARGET = -1,
    PATH_START = 0
};

enum buildingAlign_t
{
    ALIGN_IN_CORNER,
    ALIGN_IN_CORNER_CENTER,
    ALIGN_AROUND_BUILDER
};

enum player_t
{
    PLAYER_ALLY,
    PLAYER_ENEMY
};

tile_t worldMap[MAP_SIZE][MAP_SIZE];
tile_t buildMap[MAP_SIZE][MAP_SIZE];

/*
===================
Distance
===================
*/
float Distance(const Vec2Int &vec1, const Vec2Int &vec2)
{
    return sqrtf(powf((float)(vec1.x - vec2.x), 2) + powf((float)(vec1.y - vec2.y), 2));
}

/*
===================
IsAtRange
===================
*/
bool IsAtRange(const PlayerView &playerView, const Entity &entity, const Vec2Int &target, const int range)
{
    int entitySize = playerView.entityProperties.at(entity.entityType).size;

    for (int i = 0; i < entitySize; i++)
    {
        for (int j = 0; j < entitySize; j++)
        {
            for (int x = 0; x <= range; x++)
                for (int y = 0; y <= range - x; y++)
                    if (entity.position.x + i + x == target.x && entity.position.y + j + y == target.y)
                        return true;

            for (int x = -range; x <= 0; x++)
                for (int y = 0; y <= range + x; y++)
                    if (entity.position.x + i + x == target.x && entity.position.y + j + y == target.y)
                        return true;

            for (int x = 0; x <= range; x++)
                for (int y = 0; y >= -range + x; y--)
                    if (entity.position.x + i + x == target.x && entity.position.y + j + y == target.y)
                        return true;

            for (int x = -range; x <= 0; x++)
                for (int y = 0; y >= -range - x; y--)
                    if (entity.position.x + i + x == target.x && entity.position.y + j + y == target.y)
                        return true;
        }
    }

    return false;
}

/*
===================
GetNearestPosition
===================
*/
bool GetNearestPosition(const Vec2Int &from, const vector<Vec2Int> &positions, Vec2Int &nearestPosition)
{
    bool found = false;
    float nearestDistance = numeric_limits<float>::max();

    for (const auto &position : positions)
    {
        float distance = Distance(from, position);

        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestPosition.x = position.x;
            nearestPosition.y = position.y;
            found = true;
        }
    }

    return found;
}

/*
===================
GetSpawnPoints
===================
*/
void GetSpawnPoints(const PlayerView &playerView, const Entity &spawnObject, const tile_t (&map)[MAP_SIZE][MAP_SIZE], vector<Vec2Int> &spawns)
{
    spawns.clear();
    int x = spawnObject.position.x;
    int y = spawnObject.position.y;
    int size = playerView.entityProperties.at(spawnObject.entityType).size;

    for (int k = 0; x > 0 && k < size; k++)                  if (map[x - 1][y + k] == TILE_EMPTY)      spawns.push_back(Vec2Int(x - 1, y + k));
    for (int k = 0; x + size < MAP_SIZE && k < size; k++)    if (map[x + size][y + k] == TILE_EMPTY)   spawns.push_back(Vec2Int(x + size, y + k));
    for (int k = 0; y > 0 && k < size; k++)                  if (map[x + k][y - 1] == TILE_EMPTY)      spawns.push_back(Vec2Int(x + k, y - 1));
    for (int k = 0; y + size < MAP_SIZE && k < size; k++)    if (map[x + k][y + size] == TILE_EMPTY)   spawns.push_back(Vec2Int(x + k, y + size));
}

/*
===================
GetNearestSpawnPoint
===================
*/
bool GetNearestSpawnPoint(const PlayerView &playerView, const Entity &spawnObject, const Vec2Int &targetPosition, Vec2Int &spawn)
{
    vector<Vec2Int> spawns;
    GetSpawnPoints(playerView, spawnObject, worldMap, spawns);

    if (GetNearestPosition(targetPosition, spawns, spawn))
        return true;

    return false;
}

/*
===================
GetNumberOfTroops
===================
*/
int GetNumberOfTroops(const PlayerView &playerView, const Entity &fromEntity, const player_t player, int range = numeric_limits<int>::max())
{
    int troops = 0;

    for (const auto &entity : playerView.entities)
        if (entity.playerId && (*entity.playerId == playerView.myId && player == player_t::PLAYER_ALLY || *entity.playerId != playerView.myId && player == player_t::PLAYER_ENEMY) && (entity.entityType == MELEE_UNIT || entity.entityType == RANGED_UNIT))
            if (Distance(fromEntity.position, entity.position) <= range)
                troops++;

    return troops;
}

/*
===================
IsEntityCloserToPosition
===================
*/
bool IsEntityCloserToPosition(const PlayerView &playerView, const Entity &entity, const Vec2Int &position, int maxClosest = 1)
{
    for (const auto &other : playerView.entities)
    {
        if (!other.playerId || *other.playerId != playerView.myId || other.entityType != entity.entityType || other.id == entity.id)
            continue;

        if (Distance(entity.position, position) > Distance(other.position, position) && !(--maxClosest))
            return false;
    }

    return true;
}

/*
===================
MakeMap
===================
*/
void MakeMap(const PlayerView &playerView, tile_t (&map)[MAP_SIZE][MAP_SIZE], bool forBuilding = false)
{
    // Removes old data from the map
    if (playerView.fogOfWar)
    {
        for (int i = 0; i < MAP_SIZE; i++)
            for (int j = 0; j < MAP_SIZE; j++)
                if (map[i][j] != TILE_DESTROYABLE)
                    map[i][j] = TILE_EMPTY;

        // Removes resource tiles data within ally troops sight range
        for (const auto &entity : playerView.entities)
        {
            if (!entity.playerId || *entity.playerId != playerView.myId) continue;

            const EntityProperties &properties = playerView.entityProperties.at(entity.entityType);

            for (int i = 0; i < properties.size; i++)
            {
                for (int j = 0; j < properties.size; j++)
                {
                    for (int x = 0; x <= properties.sightRange; x++)
                        for (int y = 0; y <= properties.sightRange - x; y++)
                            if (entity.position.x + i + x < MAP_SIZE && entity.position.y + j + y < MAP_SIZE)
                                map[entity.position.x + i + x][entity.position.y + j + y] = TILE_EMPTY;

                    for (int x = -properties.sightRange; x <= 0; x++)
                        for (int y = 0; y <= properties.sightRange + x; y++)
                            if (entity.position.x + i + x >= 0 && entity.position.y + j + y < MAP_SIZE)
                                map[entity.position.x + i + x][entity.position.y + j + y] = TILE_EMPTY;

                    for (int x = 0; x <= properties.sightRange; x++)
                        for (int y = 0; y >= -properties.sightRange + x; y--)
                            if (entity.position.x + i + x < MAP_SIZE && entity.position.y + j + y >= 0)
                                map[entity.position.x + i + x][entity.position.y + j + y] = TILE_EMPTY;

                    for (int x = -properties.sightRange; x <= 0; x++)
                        for (int y = 0; y >= -properties.sightRange - x; y--)
                            if (entity.position.x + i + x >= 0 && entity.position.y + j + y >= 0)
                                map[entity.position.x + i + x][entity.position.y + j + y] = TILE_EMPTY;
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < MAP_SIZE; i++)
            for (int j = 0; j < MAP_SIZE; j++)
                map[i][j] = TILE_EMPTY;
    }

    // Mapping
    for (const auto &entity : playerView.entities)
    {
        const EntityProperties &properties = playerView.entityProperties.at(entity.entityType);

        // Adds an indent to our buildings for building
        if (forBuilding && (entity.entityType == BUILDER_BASE || entity.entityType == MELEE_BASE || entity.entityType == RANGED_BASE || entity.entityType == HOUSE || entity.entityType == TURRET))
        {
            int indent;

            if (playerView.fogOfWar)
            {
                indent = buildingIndentWithFog;

                // We don't need an indent when a house is located on the border of the map
                if (entity.entityType == HOUSE)
                {
                    if (entity.position.x == 0 && entity.position.y == 0)
                        indent = 0;
                    else if (entity.position.x == 0 && entity.position.y > properties.size)
                        indent = 0;
                    else if (entity.position.y == 0 && entity.position.x > properties.size + 1)
                        indent = 0;
                }
            }
            else
            {
                indent = buildingIndent;
            }

            for (int x = 0; x < properties.size + indent * 2; x++)
                for (int y = 0; y < properties.size + indent * 2; y++)
                    if (entity.position.x + x - indent >= 0 && entity.position.x + x - indent < MAP_SIZE && entity.position.y + y - indent >= 0 && entity.position.y + y - indent < MAP_SIZE)
                        map[entity.position.x + x - indent][entity.position.y + y - indent] = TILE_BLOCKED;
        }
        else
        {
            for (int x = 0; x < properties.size; x++)
            {
                for (int y = 0; y < properties.size; y++)
                {
                    if (entity.entityType == RESOURCE)
                        map[entity.position.x + x][entity.position.y + y] = TILE_DESTROYABLE;
                    else
                        map[entity.position.x + x][entity.position.y + y] = TILE_BLOCKED;
                }
            }
        }
    }
}

/*
===================
SearchPath
===================
*/
bool SearchPath(const PlayerView &playerView, int (&map)[MAP_SIZE][MAP_SIZE], vector<Vec2Int> &targetPositions, int range = numeric_limits<int>::max())
{
    int path = 0;
    targetPositions.clear();

    while (1)
    {
        bool stopSearch = true;
        bool found = false;

        if (path > range) return !targetPositions.empty();

        for (int i = 0; i < MAP_SIZE; i++)
        {
            for (int j = 0; j < MAP_SIZE; j++)
            {
                if (map[i][j] == path)
                {
                    if (i > 0 && map[i - 1][j] == PATH_EMPTY)                     map[i - 1][j] = path + 1;
                    if (i < MAP_SIZE - 1 && map[i + 1][j] == PATH_EMPTY)          map[i + 1][j] = path + 1;
                    if (j > 0 && map[i][j - 1] == PATH_EMPTY)                     map[i][j - 1] = path + 1;
                    if (j < MAP_SIZE - 1 && map[i][j + 1] == PATH_EMPTY)          map[i][j + 1] = path + 1;
                    if (i > 0 && map[i - 1][j] == PATH_TARGET)                    map[i - 1][j] = PATH_TARGET_FOUND;
                    if (i < MAP_SIZE - 1 && map[i + 1][j] == PATH_TARGET)         map[i + 1][j] = PATH_TARGET_FOUND;
                    if (j > 0 && map[i][j - 1] == PATH_TARGET)                    map[i][j - 1] = PATH_TARGET_FOUND;
                    if (j < MAP_SIZE - 1 && map[i][j + 1] == PATH_TARGET)         map[i][j + 1] = PATH_TARGET_FOUND;

                    // It takes approximately 7 ticks for destroying a resource
                    if (i > 0 && map[i - 1][j] == PATH_DESTROYABLE)               map[i - 1][j] = path + 8;
                    if (i < MAP_SIZE - 1 && map[i + 1][j] == PATH_DESTROYABLE)    map[i + 1][j] = path + 8;
                    if (j > 0 && map[i][j - 1] == PATH_DESTROYABLE)               map[i][j - 1] = path + 8;
                    if (j < MAP_SIZE - 1 && map[i][j + 1] == PATH_DESTROYABLE)    map[i][j + 1] = path + 8;

                    stopSearch = false;
                }
            }
        }

        for (int i = 0; i < MAP_SIZE; i++)
        {
            for (int j = 0; j < MAP_SIZE; j++)
            {
                if (map[i][j] == PATH_TARGET_FOUND)
                {
                    map[i][j] = path + 1;
                    targetPositions.push_back(Vec2Int(i, j));
                    found = true;
                }
            }
        }

        if (found) return true;
        if (stopSearch) return false;

        path++;
    }
}

/*
===================
SearchBuildingForRepair
===================
*/
bool SearchBuildingForRepair(const PlayerView &playerView, const Entity &builder, int &targetId, const Entity *&targetEntity, vector<Vec2Int> &positionsForBuilding, int range, const vector<EntityType> &preferedTypes = vector<EntityType>())
{
    float minDistance = numeric_limits<float>::max();
    targetEntity = nullptr;
    positionsForBuilding.clear();

    for (const auto &entity : playerView.entities)
    {
        if (!entity.playerId || *entity.playerId != playerView.myId)
            continue;

        if (!preferedTypes.empty())
        {
            bool exclude = true;

            for (const auto &preferedType : preferedTypes)
                if (entity.entityType == preferedType)
                    exclude = false;

            if (exclude) continue;
        }

        if (entity.entityType == HOUSE || entity.entityType == BUILDER_BASE || entity.entityType == MELEE_BASE || entity.entityType == RANGED_BASE || entity.entityType == TURRET)
        {
            if (entity.health < playerView.entityProperties.at(entity.entityType).maxHealth)
            {
                float entityDistance = Distance(entity.position, builder.position);

                if (entityDistance < minDistance && entityDistance < range)
                {
                    minDistance = entityDistance;
                    targetId = entity.id;
                    targetEntity = &entity;
                }
            }
        }
    }

    if (targetEntity)
    {
        worldMap[builder.position.x][builder.position.y] = TILE_EMPTY;
        GetSpawnPoints(playerView, *targetEntity, worldMap, positionsForBuilding);
        worldMap[builder.position.x][builder.position.y] = TILE_BLOCKED;

        if (!positionsForBuilding.empty()) return true;
    }

    return false;
}

/*
===================
SearchPlaceForBuilding
===================
*/
bool SearchPlaceForBuilding(const PlayerView &playerView, const Entity &builder, Vec2Int &position, vector<Vec2Int> &positionsForBuilding, EntityType type, int fromBase = 0, buildingAlign_t align = ALIGN_IN_CORNER)
{
    auto checkPlace = [](const PlayerView &playerView, const Entity &builder, tile_t (&map)[MAP_SIZE][MAP_SIZE], int x, int y, Vec2Int &position, vector<Vec2Int> &positionsForBuilding, EntityType type)
    {
        int size = playerView.entityProperties.at(type).size;
        bool canBuild = true;
        positionsForBuilding.clear();

        if (x < 0 || y < 0 || x >= MAP_SIZE - size || y >= MAP_SIZE - size) return false;

        buildMap[builder.position.x][builder.position.y] = TILE_EMPTY;

        for (int i = x; i < x + size; i++)
            for (int j = y; j < y + size; j++)
                if (buildMap[i][j] != TILE_EMPTY)
                    canBuild = false;

        if (canBuild)
        {   
            position.x = x;
            position.y = y;

            for (int k = 0; x > 0 && k < size; k++)                 if (buildMap[x - 1][y + k] == TILE_EMPTY)        positionsForBuilding.push_back(Vec2Int(x - 1, y + k));
            for (int k = 0; x + size < MAP_SIZE && k < size; k++)   if (buildMap[x + size][y + k] == TILE_EMPTY)     positionsForBuilding.push_back(Vec2Int(x + size, y + k));
            for (int k = 0; y > 0 && k < size; k++)                 if (buildMap[x + k][y - 1] == TILE_EMPTY)        positionsForBuilding.push_back(Vec2Int(x + k, y - 1));
            for (int k = 0; y + size < MAP_SIZE && k < size; k++)   if (buildMap[x + k][y + size] == TILE_EMPTY)     positionsForBuilding.push_back(Vec2Int(x + k, y + size));
        }

        buildMap[builder.position.x][builder.position.y] = TILE_BLOCKED;
        return canBuild;
    };

    switch (align)
    {
        case ALIGN_IN_CORNER:
        {
            for (int n = fromBase; n < MAP_SIZE; n++)
            {
                for (int i = 0, j = n; i <= n && j >= 0; i++, j--)
                {
                    if (checkPlace(playerView, builder, buildMap, i, j, position, positionsForBuilding, type))
                        return true;
                }
            }

            break;
        }
        case ALIGN_IN_CORNER_CENTER:
        {
            for (int n = fromBase; n < MAP_SIZE; n++)
            {
                for (int i = n / 2, j = n / 2, i2 = n / 2, j2 = n / 2; i <= n && j >= 0 && i2 >= 0 && j2 <= n; i++, j--, i2--, j2++)
                {
                    if (checkPlace(playerView, builder, buildMap, i, j, position, positionsForBuilding, type))
                        return true;
                    else if (checkPlace(playerView, builder, buildMap, i2, j2, position, positionsForBuilding, type))
                        return true;
                }
            }

            break;
        }
        case ALIGN_AROUND_BUILDER:
        {
            for (int n = 0; n < MAP_SIZE; n++)
            {
                for (int i = builder.position.x - n; i <= builder.position.x + n; i++)
                {
                    for (int j = builder.position.y - n; j <= builder.position.y + n; j++)
                    {
                        if (checkPlace(playerView, builder, buildMap, i, j, position, positionsForBuilding, type))
                            return true;
                    }
                }
            }

            break;
        }
    }

    return false;
}

/*
===================
SearchForResources
===================
*/
bool SearchForResources(const PlayerView &playerView, const Entity &builder, Vec2Int &targetPosition, int &targetId, const int range = numeric_limits<int>::max())
{
    int path[MAP_SIZE][MAP_SIZE];
    float nearestDistance = numeric_limits<float>::max();
    vector<Vec2Int> positions;

    for (int i = 0; i < MAP_SIZE; i++)
    {
        for (int j = 0; j < MAP_SIZE; j++)
        {
            if (worldMap[i][j] == TILE_EMPTY)
            {
                path[i][j] = PATH_EMPTY;
            }
            else
            {
                path[i][j] = PATH_BLOCKED;
            }
        }
    }
    
    path[builder.position.x][builder.position.y] = PATH_START;

    for (const auto &entity : playerView.entities)
        if (entity.entityType == RESOURCE)
            path[entity.position.x][entity.position.y] = PATH_TARGET;

    if (SearchPath(playerView, path, positions, range))
    {
        for (const auto &position : positions)
        {
            float distance = Distance(builder.position, position);

            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                targetPosition.x = position.x;
                targetPosition.y = position.y;

                for (const auto &entity : playerView.entities)
                    if (entity.position.x == position.x && entity.position.y == position.y)
                        targetId = entity.id;
            }
        }

        return true;
    }

    return false;
}

/*
===================
SearchForEnemies
===================
*/
bool SearchForEnemies(const PlayerView &playerView, const Entity &entity, Vec2Int &position, int &targetId, int range = numeric_limits<int>::max(), const vector<EntityType> &preferedTypes = vector<EntityType>())
{
    bool found = false;
    float MinDistance = numeric_limits<float>::max();
    int lowestHealth = numeric_limits<int>::max();

    for (const auto &enemy : playerView.entities)
    {
        if (!enemy.playerId || enemy.entityType == RESOURCE || *enemy.playerId == playerView.myId)
            continue;

        if (!preferedTypes.empty())
        {
            bool exclude = true;

            for (const auto &preferedType : preferedTypes)
                if (enemy.entityType == preferedType)
                    exclude = false;

            if (exclude) continue;
        }

        float enemyDistance = Distance(entity.position, enemy.position);

        if (entity.entityType == RANGED_UNIT || entity.entityType == TURRET)
        {
            if (entity.health < lowestHealth && IsAtRange(playerView, entity, enemy.position, playerView.entityProperties.at(entity.entityType).attack->attackRange))
            {
                lowestHealth = entity.health;
                position.x = enemy.position.x;
                position.y = enemy.position.y;
                targetId = enemy.id;
                found = true;
            }
        }

        if (lowestHealth == numeric_limits<int>::max() && enemyDistance < MinDistance && enemyDistance <= range)
        {
            MinDistance = enemyDistance;
            position.x = enemy.position.x;
            position.y = enemy.position.y;
            targetId = enemy.id;
            found = true;
        }
    }

    return found;
}

/*
===================
GetNearestEnemyPosition
===================
*/
bool GetNearestEnemyPosition(const PlayerView &playerView, Vec2Int &position, const Vec2Int fromPosition = Vec2Int(0, 0))
{
    bool found = false;
    float nearestDistance = numeric_limits<float>::max();

    for (const auto &entity : playerView.entities)
    {
        if (entity.playerId && *entity.playerId != playerView.myId && (entity.entityType == MELEE_UNIT || entity.entityType == RANGED_UNIT))
        {
            float distance = Distance(fromPosition, entity.position);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                position.x = entity.position.x;
                position.y = entity.position.y;
                found = true;
            }
        }
    }

    return found;
}

/*
===================
IsItWorthToAttack
===================
*/
bool IsItWorthToAttack(const PlayerView &playerView, const Entity &fromEntity, int allyRange, int enemyRange)
{
    int allyScore = 0;
    int enemyScore = 0;
    vector<int> counted;

    for (const auto &enemy : playerView.entities)
    {
        if (enemy.playerId && *enemy.playerId != playerView.myId && (enemy.entityType == MELEE_UNIT || enemy.entityType == RANGED_UNIT))
        {
            if (fromEntity.entityType == RANGED_UNIT && enemy.entityType == RANGED_UNIT && IsAtRange(playerView, fromEntity, enemy.position, ranged_dontRunAwayFromRanged)) return true;
            if (fromEntity.entityType == RANGED_UNIT && enemy.entityType == MELEE_UNIT && IsAtRange(playerView, fromEntity, enemy.position, ranged_dontRunAwayFromMelee)) return true;
            if (fromEntity.entityType == MELEE_UNIT && enemy.entityType == RANGED_UNIT && IsAtRange(playerView, fromEntity, enemy.position, melee_dontRunAwayFromRanged)) return true;
            if (fromEntity.entityType == MELEE_UNIT && enemy.entityType == MELEE_UNIT && IsAtRange(playerView, fromEntity, enemy.position, melee_dontRunAwayFromMelee)) return true;

            if (IsAtRange(playerView, fromEntity, enemy.position, allyRange))
            {
                if (enemy.entityType == MELEE_UNIT) enemyScore += (int)(enemy.health * enemyMeleeRunAwayMultiplier);
                else if (enemy.entityType == RANGED_UNIT) enemyScore += (int)(enemy.health * enemyRangedRunAwayMultiplier);

                for (const auto &ally : playerView.entities)
                {
                    if (ally.playerId && *ally.playerId == playerView.myId && (ally.entityType == MELEE_UNIT || ally.entityType == RANGED_UNIT))
                    {
                        if (IsAtRange(playerView, enemy, ally.position, enemyRange))
                        {
                            if (Distance(Vec2Int(0, 0), enemy.position) < Distance(Vec2Int(0, 0), ally.position))
                                return true;

                            if (find(counted.begin(), counted.end(), ally.id) == counted.end())
                            {
                                counted.push_back(ally.id);

                                if (ally.entityType == MELEE_UNIT) allyScore += (int)(ally.health * allyMeleeRunAwayMultiplier);
                                else if (ally.entityType == RANGED_UNIT) allyScore += (int)(ally.health * allyRangedRunAwayMultiplier);
                            }
                        }
                    }
                }
            }
        }
    }

    if (allyScore >= enemyScore)
        return true;
    else
        return false;
}

/*
===================
Move
===================
*/
bool Move(const PlayerView &playerView, const Entity &entity, const Vec2Int &target, Vec2Int &move)
{
    int path[MAP_SIZE][MAP_SIZE];
    const EntityProperties &properties = playerView.entityProperties.at(entity.entityType);
    vector<Vec2Int> positions;

    for (int i = 0; i < MAP_SIZE; i++)
    {
        for (int j = 0; j < MAP_SIZE; j++)
        {
            if (worldMap[i][j] == TILE_EMPTY)
                path[i][j] = PATH_EMPTY;
            else if (worldMap[i][j] == TILE_DESTROYABLE)
                path[i][j] = PATH_DESTROYABLE;
            else
                path[i][j] = PATH_BLOCKED;
        }
    }

    for (const auto &entity : playerView.entities)
    {
        const EntityProperties &property = playerView.entityProperties.at(entity.entityType);

        if (entity.entityType == RESOURCE) continue;

        if (entity.playerId && *entity.playerId == playerView.myId && (entity.entityType == RANGED_UNIT || entity.entityType == MELEE_UNIT || entity.entityType == BUILDER_UNIT))
        {
            if (unitPositionsAtLastTick[entity.position.x][entity.position.y] != unitPositionsAtCurrentTick[entity.position.x][entity.position.y])
                path[entity.position.x][entity.position.y] = PATH_EMPTY;
            else
                path[entity.position.x][entity.position.y] = PATH_BLOCKED;
        }
        else
        {
            for (int i = 0; i < property.size; i++)
                for (int j = 0; j < property.size; j++)
                    path[entity.position.x + i][entity.position.y + j] = PATH_BLOCKED;
        }
    }

    for (int i = 0; i < properties.size; i++)
        for (int j = 0; j < properties.size; j++)
            path[entity.position.x + i][entity.position.y + j] = PATH_TARGET;

    path[target.x][target.y] = PATH_START;
  
    // Makes ally troops avoid enemy turrets
    for (const auto &turret : playerView.entities)
    {
        if (turret.playerId && *turret.playerId != playerView.myId && turret.entityType == TURRET)
        {
            int entitySize = playerView.entityProperties.at(turret.entityType).size;
            int range = playerView.entityProperties.at(turret.entityType).attack->attackRange;

            for (int i = 0; i < entitySize; i++)
            {
                for (int j = 0; j < entitySize; j++)
                {
                    for (int x = 0; x <= range; x++)
                        for (int y = 0; y <= range - x; y++)
                            if (turret.position.x + i + x < MAP_SIZE && turret.position.y + j + y < MAP_SIZE)
                                path[turret.position.x + i + x][turret.position.y + j + y] = PATH_BLOCKED;

                    for (int x = -range; x <= 0; x++)
                        for (int y = 0; y <= range + x; y++)
                            if (turret.position.x + i + x >= 0 && turret.position.y + j + y < MAP_SIZE)
                                path[turret.position.x + i + x][turret.position.y + j + y] = PATH_BLOCKED;

                    for (int x = 0; x <= range; x++)
                        for (int y = 0; y >= -range + x; y--)
                            if (turret.position.x + i + x < MAP_SIZE && turret.position.y + j + y >= 0)
                                path[turret.position.x + i + x][turret.position.y + j + y] = PATH_BLOCKED;

                    for (int x = -range; x <= 0; x++)
                        for (int y = 0; y >= -range - x; y--)
                            if (turret.position.x + i + x >= 0 && turret.position.y + j + y >= 0)
                                path[turret.position.x + i + x][turret.position.y + j + y] = PATH_BLOCKED;
                }
            }
        }
    }

    // Calculates the next move position
    if (SearchPath(playerView, path, positions))
    {
        if (entity.position.x + 1 < MAP_SIZE && path[entity.position.x + 1][entity.position.y] == path[entity.position.x][entity.position.y] - 1)
        {
            move.x = entity.position.x + 1;
            move.y = entity.position.y;
        }
        else if (entity.position.y + 1 < MAP_SIZE && path[entity.position.x][entity.position.y + 1] == path[entity.position.x][entity.position.y] - 1)
        {
            move.x = entity.position.x;
            move.y = entity.position.y + 1;
        }
        else if (entity.position.x - 1 >= 0 && path[entity.position.x - 1][entity.position.y] == path[entity.position.x][entity.position.y] - 1)
        {
            move.x = entity.position.x - 1;
            move.y = entity.position.y;
        }
        else if (entity.position.y - 1 >= 0 && path[entity.position.x][entity.position.y - 1] == path[entity.position.x][entity.position.y] - 1)
        {
            move.x = entity.position.x;
            move.y = entity.position.y - 1;
        }

        return true;
    }

    return false;
}

/*
===================
MyStrategy
===================
*/
MyStrategy::MyStrategy()
{
    for (int i = 0; i < MAP_SIZE; i++)
        for (int j = 0; j < MAP_SIZE; j++)
            unitPositionsAtLastTick[i][j] = unitPositionsAtCurrentTick[i][j] = 0;
}

/*
===================
getAction
===================
*/
Action MyStrategy::getAction(const PlayerView &playerView, DebugInterface *debugInterface)
{
    Action result = Action(unordered_map<int, EntityAction>());
    int mapSize = playerView.mapSize;
    int myId = playerView.myId;
    int score = playerView.players[myId - 1].score;
    int resources = playerView.players[myId - 1].resource;
    int population = 0;
    int maxPopulation = 0;
    int numOfTroops = 0;
    int numOfBuilders = 0;
    int numOfMelee = 0;
    int numOfRanged = 0;
    int numOfBuilderBases = 0;
    int numOfMeleeBases = 0;
    int numOfRangedBases = 0;
    int numOfHouses = 0;
    int numOfResources = 0;
    int numOfTurrets = 0;
    int baseSize = 0;
    int farthestBuilder = 0;
    int targetId = 0;
    float entitiesRatio = 0.0f;
    Vec2Int spawnPoint;
    Vec2Int targetPosition;
    Vec2Int positionForBuilding;
    Vec2Int movePosition;
    vector<Vec2Int> positionsForBuilding;
    const Entity *targetEntity = nullptr;
    const EntityProperties &builder = playerView.entityProperties.at(BUILDER_UNIT);
    const EntityProperties &melee = playerView.entityProperties.at(MELEE_UNIT);
    const EntityProperties &ranged = playerView.entityProperties.at(RANGED_UNIT);
    const EntityProperties &builderBase = playerView.entityProperties.at(BUILDER_BASE);
    const EntityProperties &meleeBase = playerView.entityProperties.at(MELEE_BASE);
    const EntityProperties &rangedBase = playerView.entityProperties.at(RANGED_BASE);
    const EntityProperties &house = playerView.entityProperties.at(HOUSE);
    const EntityProperties &turret = playerView.entityProperties.at(TURRET);
    const unordered_map<EntityType, EntityProperties> &entityProperties = playerView.entityProperties;
    const vector<Entity> &entities = playerView.entities;

    static int currentTick = 0;

    if (playerView.currentTick == currentTick)
    {
        for (int i = 0; i < MAP_SIZE; i++)
        {
            for (int j = 0; j < MAP_SIZE; j++)
            {
                unitPositionsAtLastTick[i][j] = unitPositionsAtCurrentTick[i][j];
                unitPositionsAtCurrentTick[i][j] = 0;
            }
        }

        // Adds known enemy spawn positions when the fog of war is enabled
        if (currentTick == 0 && playerView.fogOfWar)
        {
            if (playerView.players.size() > 2)
            {
                knownEnemySpawns.push_back(Vec2Int(MAP_SIZE - 1, 0));
                knownEnemySpawns.push_back(Vec2Int(0, MAP_SIZE - 1));
            }

            knownEnemySpawns.push_back(Vec2Int(MAP_SIZE - 1, MAP_SIZE - 1));
        }

        MakeMap(playerView, worldMap);
        MakeMap(playerView, buildMap, true);

        for (const auto &entity : playerView.entities)
        {
            const EntityProperties &properties = playerView.entityProperties.at(entity.entityType);

            if (entity.entityType == RESOURCE) numOfResources++;
            if (!entity.playerId) continue;

            if (*entity.playerId == myId)
            {
                if (entity.entityType == BUILDER_UNIT || entity.entityType == MELEE_UNIT || entity.entityType == RANGED_UNIT)
                    unitPositionsAtCurrentTick[entity.position.x][entity.position.y] = entity.id;

                if (playerView.fogOfWar)
                {
                    // Removes the known enemy positions if they are not in the fog of war
                    for (auto it = knownEnemies.begin(); it != knownEnemies.end();)
                    {
                        if (IsAtRange(playerView, entity, *it, properties.sightRange))
                        {
                            bool exists = false;

                            for (const auto &enemy : playerView.entities)
                                if (enemy.playerId && *enemy.playerId != myId && enemy.position.x == it->x && enemy.position.y == it->y)
                                    exists = true;

                            if (!exists)
                            {
                                it = knownEnemies.erase(it);
                                continue;
                            }
                        }

                        it++;
                    }

                    // Removes the known enemy spawns if they are not in the fog of war
                    for (auto it = knownEnemySpawns.begin(); it != knownEnemySpawns.end();)
                    {
                        if (Distance(entity.position, *it) <= properties.sightRange)
                            it = knownEnemySpawns.erase(it);
                        else
                            it++;
                    }
                }

                // Calculate base size and the farthest builder
                if ((entity.entityType == BUILDER_BASE || entity.entityType == MELEE_BASE || entity.entityType == RANGED_BASE || entity.entityType == HOUSE))
                {
                    float distance = Distance(Vec2Int(0, 0), Vec2Int(entity.position.x, entity.position.y));
                    if (distance > baseSize) baseSize = (int)distance;
                }
                else if (entity.entityType == BUILDER_UNIT)
                {
                    float distance = Distance(Vec2Int(0, 0), entity.position);
                    if (distance > farthestBuilder) farthestBuilder = (int)distance;
                }

                population += properties.populationUse;
                maxPopulation += properties.populationProvide;

                if (properties.canMove && properties.attack && !(properties.build || properties.repair)) numOfTroops++;
                if (entity.entityType == BUILDER_UNIT) numOfBuilders++;
                if (entity.entityType == MELEE_UNIT) numOfMelee++;
                if (entity.entityType == RANGED_UNIT) numOfRanged++;
                if (entity.entityType == BUILDER_BASE) numOfBuilderBases++;
                if (entity.entityType == MELEE_BASE) numOfMeleeBases++;
                if (entity.entityType == RANGED_BASE) numOfRangedBases++;
                if (entity.entityType == HOUSE) numOfHouses++;
                if (entity.entityType == TURRET) numOfTurrets++;
            }
            // Saves the last known enemy positions
            else if (playerView.fogOfWar)
            {
                bool exists = false;

                for (auto &enemy : knownEnemies)
                    if (enemy.x == entity.position.x && enemy.y == entity.position.y)
                        exists = true;

                if (!exists && entity.entityType != TURRET) knownEnemies.push_back(entity.position);
            }
        }

        if (numOfMeleeBases || numOfRangedBases) entitiesRatio = troopsBuildersRatio;

        currentTick++;
    }

    // Main logic
    for (const auto &entity : playerView.entities)
    {
        if (!entity.playerId || *entity.playerId != myId) continue;

        const EntityProperties &properties = playerView.entityProperties.at(entity.entityType);
        shared_ptr<MoveAction> moveAction = nullptr;
        shared_ptr<BuildAction> buildAction = nullptr;
        shared_ptr<AttackAction> attackAction = nullptr;
        shared_ptr<RepairAction> repairAction = nullptr;

        /*
        ===================================================================================================
            BUILDER BASE
        ===================================================================================================
        */
        if (entity.entityType == BUILDER_BASE && resources >= builder.buildScore)
        {
            if (!SearchForEnemies(playerView, entity, targetPosition, targetId, baseSize + builderBase.sightRange) && ((!numOfMeleeBases && !numOfRangedBases) || resources >= melee.buildScore + builder.buildScore) && ((float)((float)numOfBuilders / (float)maxPopulation) < 1.0f - entitiesRatio))
                if (SearchForResources(playerView, entity, targetPosition, targetId))
                    if (GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint))
                        buildAction = shared_ptr<BuildAction>(new BuildAction(BUILDER_UNIT, spawnPoint));
        }
        /*
        ===================================================================================================
            MELEE BASE

            We don't need to build melee. We can win the game without them.
        ===================================================================================================
        */
        else if (entity.entityType == MELEE_BASE)
        {
            if (!numOfRangedBases && (((float)((float)numOfTroops / (float)maxPopulation) < entitiesRatio)))
            {
                if (SearchForEnemies(playerView, entity, targetPosition, targetId, mapSize) && GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint)) {}
                else if (!knownEnemies.empty() && GetNearestPosition(entity.position, knownEnemies, targetPosition) && GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint)) {}
                else if (!knownEnemySpawns.empty() && GetNearestPosition(entity.position, knownEnemySpawns, targetPosition) && GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint)) {}

                buildAction = shared_ptr<BuildAction>(new BuildAction(MELEE_UNIT, spawnPoint));
            }

            // Spawn melee units when enemy troops are in our base.
            if (/*!numOfRangedBases && */SearchForEnemies(playerView, entity, targetPosition, targetId, baseSize + meleeBase.sightRange))
                if (GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint))
                    buildAction = shared_ptr<BuildAction>(new BuildAction(MELEE_UNIT, spawnPoint));
        }
        /*
        ===================================================================================================
            RANGED BASE
        ===================================================================================================
        */
        else if (entity.entityType == RANGED_BASE)
        {
            if ((((float)((float)numOfTroops / (float)maxPopulation) < entitiesRatio)))
            {
                if (SearchForEnemies(playerView, entity, targetPosition, targetId, mapSize) && GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint)) {}
                else if (!knownEnemies.empty() && GetNearestPosition(entity.position, knownEnemies, targetPosition) && GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint)) {}
                else if (!knownEnemySpawns.empty() && GetNearestPosition(entity.position, knownEnemySpawns, targetPosition) && GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint)) {}

                buildAction = shared_ptr<BuildAction>(new BuildAction(RANGED_UNIT, spawnPoint));
            }

            // Spawn ranged units when enemy troops are in our base.
            if (SearchForEnemies(playerView, entity, targetPosition, targetId, baseSize + rangedBase.sightRange))
                if (GetNearestSpawnPoint(playerView, entity, targetPosition, spawnPoint))
                    buildAction = shared_ptr<BuildAction>(new BuildAction(RANGED_UNIT, spawnPoint));
        }
        /*
        ===================================================================================================
            BUILDERS
        ===================================================================================================
        */
        else if (entity.entityType == BUILDER_UNIT)
        {
            // Attack enemy builders when there're no more resources left
            if (/*!playerView.fogOfWar && */!numOfResources && SearchForEnemies(playerView, entity, targetPosition, targetId, mapSize, { BUILDER_UNIT }))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { BUILDER_UNIT }))));
            }
            // Attack enemies bases when there're no more resources left
            else if (/*!playerView.fogOfWar && */!numOfResources && SearchForEnemies(playerView, entity, targetPosition, targetId, mapSize, { BUILDER_BASE, MELEE_BASE, RANGED_BASE, HOUSE }))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { BUILDER_BASE, MELEE_BASE, RANGED_BASE, HOUSE }))));
            }
            // Attack other enemies when there're no more resources left
            else if (/*!playerView.fogOfWar && */!numOfResources && SearchForEnemies(playerView, entity, targetPosition, targetId, mapSize, { MELEE_UNIT, RANGED_UNIT }))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { MELEE_UNIT, RANGED_UNIT }))));
            }
            // Attack enemies at base when there're no troops
            /*else if (!numOfTroops && SearchForEnemies(playerView, entity, position, targetId, baseSize, { BUILDER_UNIT, MELEE_UNIT, RANGED_UNIT }))
            {
                if (Move(playerView, entity, position, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { BUILDER_UNIT, MELEE_UNIT, RANGED_UNIT }))));
            }*/
            // Attack near builders
            else if (SearchForEnemies(playerView, entity, targetPosition, targetId, builderAttackBuilderDistance, { BUILDER_UNIT }) && !GetNumberOfTroops(playerView, entity, player_t::PLAYER_ALLY, buildersRunAwayDistance))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { BUILDER_UNIT }))));
            }
            // Attack near enemies bases when there're no enemy troops
            else if (SearchForEnemies(playerView, entity, targetPosition, targetId, builderAttackBuilderDistance, { BUILDER_BASE, MELEE_BASE, RANGED_BASE }) && !GetNumberOfTroops(playerView, entity, player_t::PLAYER_ALLY, buildersRunAwayDistance))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { BUILDER_BASE, MELEE_BASE, RANGED_BASE }))));
            }
            // Build/Repair buildings
            else if ((resources >= 50 || numOfBuilders > 1) &&
                (!numOfRangedBases && SearchBuildingForRepair(playerView, entity, targetId, targetEntity, positionsForBuilding, mapSize, { MELEE_BASE }) || SearchBuildingForRepair(playerView, entity, targetId, targetEntity, positionsForBuilding, builderRepairDistance, { BUILDER_BASE, RANGED_BASE, HOUSE, TURRET })) &&
                IsEntityCloserToPosition(playerView, entity, targetEntity->position, numberOfBuildersForRepair))
            {
                if (GetNearestPosition(entity.position, positionsForBuilding, positionForBuilding))
                {
                    if (Move(playerView, entity, positionForBuilding, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                    repairAction = shared_ptr<RepairAction>(new RepairAction(targetId));
                }
            }
            // Attack enemies at base
            /*else if (entity.position.x < baseSize && entity.position.y < baseSize && SearchForEnemies(playerView, entity, position, targetId, baseSize, { MELEE_UNIT, RANGED_UNIT }))
            {
                if (Move(playerView, entity, position, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                //moveAction = shared_ptr<MoveAction>(new MoveAction(position, true, true));
                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { MELEE_UNIT, RANGED_UNIT }))));
            }*/
            // Run away from enemy troops
            else if (!GetNumberOfTroops(playerView, entity, player_t::PLAYER_ALLY, 1) && !SearchForResources(playerView, entity, targetPosition, targetId, 1) && SearchForEnemies(playerView, entity, targetPosition, targetId, buildersRunAwayDistance, { MELEE_UNIT, RANGED_UNIT, TURRET }))
            {
                Vec2Int to(entity.position.x - (targetPosition.x - entity.position.x), entity.position.y - (targetPosition.y - entity.position.y));

                if (to.x < 0) to.x = 0;
                if (to.x >= MAP_SIZE) to.x = MAP_SIZE - 1;
                if (to.y < 0) to.y = 0;
                if (to.y >= MAP_SIZE) to.y = MAP_SIZE - 1;

                if (Move(playerView, entity, to, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));
            }
            // Building builder base if it doesn't exist
            else if (!numOfBuilderBases && resources >= builderBase.buildScore && SearchPlaceForBuilding(playerView, entity, positionForBuilding, positionsForBuilding, BUILDER_BASE) && IsEntityCloserToPosition(playerView, entity, positionForBuilding))
            {
                if (GetNearestPosition(entity.position, positionsForBuilding, targetPosition))
                {
                    if (Move(playerView, entity, targetPosition, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                    buildAction = shared_ptr<BuildAction>(new BuildAction(BUILDER_BASE, positionForBuilding));
                }
            }
            // Building melee base if it doesn't exist
            /*else if (!numOfMeleeBases && resources >= meleeBase.buildScore && SearchPlaceForBuilding(playerView, entity, positionForBuilding, positionsForBuilding, MELEE_BASE, false, baseSize, true) && IsEntityCloserToPosition(playerView, entity, positionForBuilding))
            {
                if (GetNearestPosition(entity.position, positionsForBuilding, targetPosition))
                {
                    if (Move(playerView, entity, targetPosition, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                    buildAction = shared_ptr<BuildAction>(new BuildAction(MELEE_BASE, positionForBuilding));
                }
            }*/
            // Building ranged base if it doesn't exist
            else if (!numOfRangedBases && resources >= rangedBase.buildScore && SearchPlaceForBuilding(playerView, entity, positionForBuilding, positionsForBuilding, RANGED_BASE, baseSize) && IsEntityCloserToPosition(playerView, entity, positionForBuilding))
            {
                if (GetNearestPosition(entity.position, positionsForBuilding, targetPosition))
                {
                    if (Move(playerView, entity, targetPosition, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                    buildAction = shared_ptr<BuildAction>(new BuildAction(RANGED_BASE, positionForBuilding));
                }
            }
            // Building houses
            else if (((!playerView.fogOfWar || numOfRangedBases || numOfMeleeBases) || (!numOfRangedBases && !numOfMeleeBases)) && (resources >= house.buildScore * (numOfHouses + 1)) && ((population < 30 && population >= maxPopulation - house.populationProvide) || (population >= 30 && population >= maxPopulation - house.populationProvide)) && SearchPlaceForBuilding(playerView, entity, positionForBuilding, positionsForBuilding, HOUSE) && IsEntityCloserToPosition(playerView, entity, positionForBuilding))
            {
                if (GetNearestPosition(entity.position, positionsForBuilding, targetPosition))
                {
                    if (Move(playerView, entity, targetPosition, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                    buildAction = shared_ptr<BuildAction>(new BuildAction(HOUSE, positionForBuilding));
                }
            }
            // Gather resources
            else if (SearchForResources(playerView, entity, targetPosition, targetId))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));
                
                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), nullptr));
            }
            // If builders don't see any resources or enemies, send them to any enemy base
            else
            {
                if (GetNearestPosition(entity.position, knownEnemySpawns, targetPosition))
                {
                    if (playerView.fogOfWar)
                    {
                        if (Move(playerView, entity, knownEnemySpawns[knownEnemySpawns.size() == 1 ? 0 : entity.id % 2], movePosition))
                            moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));
                    }
                    else
                    {
                        if (Move(playerView, entity, targetPosition, movePosition))
                            moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));
                    }
                }
            }
        }
        /*
        ===================================================================================================
            MELEE & RANGED UNITS
        ===================================================================================================
        */
        else if (entity.entityType == MELEE_UNIT || entity.entityType == RANGED_UNIT)
        {
            // Run away from enemy troops when it is not worth it
            if (Distance(Vec2Int(0, 0), entity.position) > baseSize + ranged.sightRange && !IsItWorthToAttack(playerView, entity, 7, 7))
            {
                moveAction = shared_ptr<MoveAction>(new MoveAction(Vec2Int(0, 0), true, true));
            }
            // Attack the nearest builder base using only the ranged units
            else if (entity.entityType == RANGED_UNIT && ((float)numOfTroops / (float)maxPopulation >= entitiesRatio || entity.position.x > baseSize && entity.position.y > baseSize) && SearchForEnemies(playerView, entity, targetPosition, targetId, troopsAttackBaseDistance, { BUILDER_BASE }))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), nullptr));
            }
            // Attack the nearest builder if there're no nearby enemy troops
            else if (((float)numOfTroops / (float)maxPopulation >= entitiesRatio || entity.position.x > baseSize && entity.position.y > baseSize) && SearchForEnemies(playerView, entity, targetPosition, targetId, troopsAttackBuilderDistance, { BUILDER_UNIT }) && !GetNumberOfTroops(playerView, entity, player_t::PLAYER_ENEMY, enemyRunAwayRange))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), nullptr));
            }
            // Attack the nearest melee/ranged bases using only the ranged units
            else if (entity.entityType == RANGED_UNIT && ((float)numOfTroops / (float)maxPopulation >= entitiesRatio || entity.position.x > baseSize && entity.position.y > baseSize) && SearchForEnemies(playerView, entity, targetPosition, targetId, troopsAttackBaseDistance, { MELEE_BASE, RANGED_BASE }))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), nullptr));
            }
            // Attack the nearest enemy
            else if (SearchForEnemies(playerView, entity, targetPosition, targetId, 99999, { BUILDER_UNIT, MELEE_UNIT, RANGED_UNIT, BUILDER_BASE, MELEE_BASE, RANGED_BASE, HOUSE, WALL }))
            {
                if (Move(playerView, entity, targetPosition, movePosition))
                    moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));

                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), nullptr));
            }
            // Move to the last known enemy positions on the map if we don't see them anymore
            else if (playerView.fogOfWar && !SearchForEnemies(playerView, entity, targetPosition, targetId, 99999, { BUILDER_UNIT, MELEE_UNIT, RANGED_UNIT, BUILDER_BASE, MELEE_BASE, RANGED_BASE, HOUSE, WALL }) && !knownEnemies.empty())
            {
                if (GetNearestPosition(entity.position, knownEnemies, targetPosition))
                {
                    if (Move(playerView, entity, targetPosition, movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));
                }
            }
            // Move to the known enemy spawns if we don't see enemies
            else if (playerView.fogOfWar && !SearchForEnemies(playerView, entity, targetPosition, targetId, 99999, { BUILDER_UNIT, MELEE_UNIT, RANGED_UNIT, BUILDER_BASE, MELEE_BASE, RANGED_BASE, HOUSE, WALL }))
            {
                if (GetNearestPosition(entity.position, knownEnemySpawns, targetPosition))
                {
                    if (Move(playerView, entity, knownEnemySpawns[knownEnemySpawns.size() == 1 ? 0 : entity.id % 2], movePosition))
                        moveAction = shared_ptr<MoveAction>(new MoveAction(movePosition, false, true));
                }
            }
        }
        /*
        ===================================================================================================
            TURRET
        ===================================================================================================
        */
        else if (entity.entityType == TURRET)
        {
            if (SearchForEnemies(playerView, entity, targetPosition, targetId, turret.attack->attackRange))
                attackAction = shared_ptr<AttackAction>(new AttackAction(shared_ptr<int>(new int(targetId)), shared_ptr<AutoAttack>(new AutoAttack(properties.sightRange, { BUILDER_UNIT, MELEE_UNIT, RANGED_UNIT, BUILDER_BASE, MELEE_BASE, RANGED_BASE, HOUSE, WALL, TURRET }))));
        }

        result.entityActions[entity.id] = EntityAction(moveAction, buildAction, attackAction, repairAction);
    }

    return result;
}

/*
===================
debugUpdate
===================
*/
void MyStrategy::debugUpdate(const PlayerView &playerView, DebugInterface &debugInterface)
{
    debugInterface.send(DebugCommand::Clear());
    debugInterface.getState();

    //debugInterface.send(DebugCommand::Add(shared_ptr<DebugData>(new DebugData::Log(string("Test")))));
}