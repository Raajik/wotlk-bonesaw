/*
 * Bonesaw docker build: cluster sidecar disabled (no libsidecar).
 */

#include "TC9Sidecar.h"

ToCloud9Sidecar* ToCloud9Sidecar::instance()
{
    static ToCloud9Sidecar instance;
    return &instance;
}

ToCloud9Sidecar::ToCloud9Sidecar() : _clusterModeEnabled(false), _isCrossrealm(false)
{
    for (int i = 0; i < MAX_MAP_ID; ++i)
        _assignedMapsByID[i] = false;
}

void ToCloud9Sidecar::Init(uint16 /*port*/, int /*realmId*/)
{
    _clusterModeEnabled = false;
    _isCrossrealm = false;
}

void ToCloud9Sidecar::Deinit()
{
}

void ToCloud9Sidecar::SetupHooks()
{
}

void ToCloud9Sidecar::SetupGrpcHandlers()
{
}

void ToCloud9Sidecar::ProcessHooks()
{
}

void ToCloud9Sidecar::ProcessGrpcOrHttpRequests()
{
}

void ToCloud9Sidecar::ProcessAsyncTasks()
{
    _asyncTasksProcessor.ProcessReadyCallbacks();
}

bool ToCloud9Sidecar::IsMapAssigned(uint32 /*mapId*/)
{
    return true;
}

uint32 ToCloud9Sidecar::GenerateCharacterGuid(uint16 /*realmId*/)
{
    return 0;
}

uint32 ToCloud9Sidecar::GenerateItemGuid(uint16 /*realmId*/)
{
    return 0;
}

uint32 ToCloud9Sidecar::GenerateInstanceGuid(uint16 /*realmId*/)
{
    return 0;
}

void ToCloud9Sidecar::OnPlayerLeftBattleground(uint64 /*playerGUID*/, uint32 /*realmID*/, uint32 /*instanceID*/)
{
}

void ToCloud9Sidecar::OnBattlegroundStatusChanged(uint32 /*instanceID*/, uint8 /*status*/)
{
}

bool ToCloud9Sidecar::NatsPublish(std::string const& /*subject*/, std::string const& /*payload*/)
{
    return false;
}

bool ToCloud9Sidecar::NatsSubscribe(std::string const& /*subject*/, void (* /*handler*/)(char const*, char const*, int))
{
    return false;
}

void ToCloud9Sidecar::OnMapsReassigned(uint32* /*addedMaps*/, int /*addedMapsSize*/, uint32* /*removedMaps*/, int /*removedMapsSize*/)
{
}
