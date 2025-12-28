#pragma once

#pragma pack(push, 1)
namespace sead {
    struct SafeString : BETypeCompatible {
        BEType<uint32_t> c_str;
        BEType<uint32_t> vtable;
    };

    struct BufferedSafeString : SafeString {
        BEType<int32_t> length;
    };
    static_assert(sizeof(BufferedSafeString) == 0x0C, "BufferedSafeString size mismatch");

    struct FixedSafeString40 : BufferedSafeString {
        char data[0x40];

        std::string getLE() const {
            if (c_str.getLE() == 0) {
                return std::string();
            }
            return std::string(data, strnlen(data, sizeof(data)));
        }
    };
    static_assert(sizeof(FixedSafeString40) == 0x4C, "FixedSafeString40 size mismatch");

	struct FixedSafeString100 : BufferedSafeString {
        char data[0x100];

        std::string getLE() const {
            if (c_str.getLE() == 0) {
                return std::string();
            }
            return std::string(data, strnlen(data, sizeof(data)));
        }
    };
    static_assert(sizeof(FixedSafeString100) == 0x10C, "FixedSafeString100 size mismatch");

    struct PtrArrayImpl {
        BEType<uint32_t> size;
        BEType<uint32_t> capacity;
        BEType<uint32_t> data;
    };
};

struct ActorPhysics {
    BEType<uint32_t> __vftable;
    sead::SafeString actorName;
    BEType<uint32_t> *physicsParamSet;
    BEType<uint32_t> flags;
    BEType<uint32_t> dword14;
    BEType<uint32_t> dword18;
    BEType<float> scale;
    BEType<uint32_t> gsysModel;
    sead::PtrArrayImpl rigidBodies;
    sead::PtrArrayImpl collisionInfo;
    sead::PtrArrayImpl contactInfo;
};

struct BaseProc {
    BEType<uint32_t> secondVTable;
    sead::FixedSafeString40 name;
    BEType<uint32_t> id;
    BEType<uint8_t> state;
    BEType<uint8_t> prio;
    BEType<uint8_t> unk_52;
    BEType<uint8_t> unk_53;
    PADDED_BYTES(0x54, 0xE0);
    BEType<uint32_t> vtable;
};
static_assert(sizeof(BaseProc) == 0xEC, "BaseProc size mismatch");

enum ActorFlags : int32_t {
    ActorFlags_1 = 0x1,
    ActorFlags_PhysicsPauseDisable = 0x2,
    ActorFlags_SetPhysicsMtx = 0x4,
    ActorFlags_DisableUpdateMtxFromPhysics = 0x8,
    ActorFlags_UndispCut = 0x10,
    ActorFlags_ModelBind = 0x20,
    ActorFlags_ParamW = 0x40,
    ActorFlags_80 = 0x80,
    ActorFlags_100 = 0x100,
    ActorFlags_200 = 0x200,
    ActorFlags_400 = 0x400,
    ActorFlags_KeepLinkTagOrAreaAliveMaybe = 0x4000,
    ActorFlags_EnableForbidPushJob = 0x40000,
    ActorFlags_DisableForbidPushJob = 0x80000,
    ActorFlags_IsLinkTag_ComplexTag_EventTag_AreaManagement = 0x800000,
    ActorFlags_OnlyPushJobType4 = 0x1000000,
    ActorFlags_VillagerRegistered = 0x4000000,
    ActorFlags_CheckDeleteDistanceEveryFrame = 0x8000000,
    ActorFlags_ForceCalcInEvent = 0x10000000,
    ActorFlags_IsCameraOrEditCamera = 0x20000000,
    ActorFlags_InitializedMaybe = 0x40000000,
    ActorFlags_PrepareForDeleteMaybe = 0x80000000,
};

enum ActorFlags2 : int32_t {
    ActorFlags2_1 = 0x1,
    ActorFlags2_2 = 0x2,
    ActorFlags2_4 = 0x4,
    ActorFlags2_InstEventFlag = 0x8,
    ActorFlags2_10 = 0x10,
    ActorFlags2_Invisible = 0x20,
    ActorFlags2_40 = 0x40,
    ActorFlags2_NoDistanceCheck = 0x80,
    ActorFlags2_AlwaysPushJobs = 0x100,
    ActorFlags2_200_KeepAliveMaybe = 0x200,
    ActorFlags2_400 = 0x400,
    ActorFlags2_Armor = 0x800,
    ActorFlags2_NotXXX = 0x1000,
    ActorFlags2_ForbidSystemDeleteDistance = 0x2000,
    ActorFlags2_4000 = 0x4000,
    ActorFlags2_Delete = 0x8000,
    ActorFlags2_10000 = 0x10000,
    ActorFlags2_20000 = 0x20000,
    ActorFlags2_40000 = 0x40000,
    ActorFlags2_NoDistanceCheck2 = 0x80000,
    ActorFlags2_100000 = 0x100000,
    ActorFlags2_200000 = 0x200000,
    ActorFlags2_StasisableOrAllRadar = 0x400000,
    ActorFlags2_800000_NoUnloadForTurnActorBowCharge = 0x800000,
    ActorFlags2_1000000 = 0x1000000,
    ActorFlags2_2000000 = 0x2000000,
    ActorFlags2_4000000 = 0x4000000,
    ActorFlags2_8000000 = 0x8000000,
    ActorFlags2_10000000 = 0x10000000,
    ActorFlags2_20000000 = 0x20000000,
    ActorFlags2_40000000 = 0x40000000,
    ActorFlags2_80000000 = 0x80000000,
};

enum ActorFlags3 : int32_t {
    ActorFlags3_DisableHideNonDemoMember = 0x1,
    ActorFlags3_2 = 0x2,
    ActorFlags3_4 = 0x4,
    ActorFlags3_8 = 0x8,
    ActorFlags3_10 = 0x10,
    ActorFlags3_20 = 0x20,
    ActorFlags3_40 = 0x40,
    ActorFlags3_80_KeepAliveMaybe = 0x80,
    ActorFlags3_100 = 0x100,
    ActorFlags3_200 = 0x200,
    ActorFlags3_400 = 0x400,
    ActorFlags3_Invisible = 0x800,
    ActorFlags3_1000 = 0x1000,
    ActorFlags3_DisableForbidJob = 0x2000,
    ActorFlags3_4000 = 0x4000,
    ActorFlags3_8000 = 0x8000,
    ActorFlags3_10000 = 0x10000,
    ActorFlags3_20000 = 0x20000,
    ActorFlags3_40000 = 0x40000,
    ActorFlags3_80000 = 0x80000,
    ActorFlags3_100000 = 0x100000,
    ActorFlags3_EmitCreateEffectMaybe = 0x200000,
    ActorFlags3_EmitDeleteEffectMaybe = 0x400000,
    ActorFlags3_800000 = 0x800000,
    ActorFlags3_ND = 0x1000000,
    ActorFlags3_GuardianOrRemainsOrBackseatKorok = 0x2000000,
    ActorFlags3_4000000 = 0x4000000,
    ActorFlags3_8000000 = 0x8000000,
    ActorFlags3_10000000 = 0x10000000,
    ActorFlags3_20000000 = 0x20000000,
    ActorFlags3_40000000 = 0x40000000,
    ActorFlags3_80000000 = 0x80000000,
};

struct ActorWiiU : BaseProc {
    PADDED_BYTES(0xEC, 0xF0);
    uint32_t physicsMainBodyPtr; // 0xF4
    uint32_t physicsTgtBodyPtr; // 0xF8
    uint8_t unk_FC[0x1F8 - 0xFC];
    BEMatrix34 mtx;
    uint32_t physicsMtxPtr;
    BEMatrix34 homeMtx;
    BEVec3 velocity;
    BEVec3 angularVelocity;
    BEVec3 scale; // 0x274
    BEType<float> dispDistSq;
    BEType<float> deleteDistSq;
    BEType<float> loadDistP10;
    BEVec3 previousPos;
    BEVec3 previousPos2;
    PADDED_BYTES(0x2A4, 0x324);
    BEType<uint32_t> modelBindInfoPtr;
    PADDED_BYTES(0x32C, 0x32C);
    BEType<uint32_t> gsysModelPtr;
    BEType<float> opacityOneRelated;
    BEType<float> startModelOpacity;
    BEType<float> modelOpacity;
    BEType<float> modelOpacityRelated;
    PADDED_BYTES(0x344, 0x348);
    struct {
        BEType<float> minX;
        BEType<float> minY;
        BEType<float> minZ;
        BEType<float> maxX;
        BEType<float> maxY;
        BEType<float> maxZ;
    } aabb;
    BEType<ActorFlags2> flags2;
    BEType<ActorFlags2> flags2Copy;
    BEType<ActorFlags> flags;
    BEType<ActorFlags3> flags3; // 0x370 or 880. However in IDA there's a 0xF4 offset

    PADDED_BYTES(0x374, 0x39C);
    BEType<uint32_t> actorPhysicsPtr; // 0x3A0
    PADDED_BYTES(0x3A4, 0x404);

    BEType<uint32_t> hashId;
    PADDED_BYTES(0x40C, 0x430);
    uint8_t unk_434;
    uint8_t unk_435;
    uint8_t opacityOrDoFlushOpacityToGPU;
    uint8_t unk_437;
    PADDED_BYTES(0x438, 0x440);
    BEType<uint32_t> actorX6A0Ptr;
    BEType<uint32_t> chemicalsPtr;
    BEType<uint32_t> reactionsPtr;
    PADDED_BYTES(0x450, 0x48C);
    BEType<float> lodDrawDistanceMultiplier;
    PADDED_BYTES(0x494, 0x538);
};
static_assert(offsetof(ActorWiiU, gsysModelPtr) == 0x330, "ActorWiiU.gsysModelPtr offset mismatch");
static_assert(offsetof(ActorWiiU, modelOpacity) == 0x33C, "ActorWiiU.modelOpacity offset mismatch");
static_assert(offsetof(ActorWiiU, modelOpacityRelated) == 0x340, "ActorWiiU.modelOpacityRelated offset mismatch");
static_assert(offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU) == 0x436, "ActorWiiU.opacityOrDoFlushOpacityToGPU offset mismatch");
static_assert(sizeof(ActorWiiU) == 0x53C, "ActorWiiU size mismatch");

struct DynamicActor : ActorWiiU {
    PADDED_BYTES(0x53C, 0x7C8);
};
static_assert(sizeof(DynamicActor) == 0x7CC, "DynamicActor size mismatch");

struct ActorWeapon {
    PADDED_BYTES(0x00, 0x0C);
};

struct ActorWeapons {
    ActorWeapon weapons[6];
    BEType<uint32_t> actorThisPtr;
    BEType<uint32_t> actorWeaponsVtblPtr;
};
static_assert(sizeof(ActorWeapons) == 0x068, "ActorWeapons size mismatch");

struct PlayerOrEnemy : DynamicActor, ActorWeapons {
    BEType<float> float834;
};
static_assert(sizeof(PlayerOrEnemy) == 0x838, "PlayerOrEnemy size mismatch");

// 0x18000021 for carrying the electricity balls at least
// 0x00000010 for ladder
// 0x00000012 for ladder transition
// 0x18000002 for jumping
// 0x00008002 for gliding
// 0x00000090 for climbing
// 0x00000410 for swimming (00001000001000000000000000000000)
enum class PlayerMoveBitFlags : uint32_t {
    IS_MOVING = 1 << 0,
    IN_AIR_MAYBE_02 = 1 << 1,
    UNK_004 = 1 << 2,
    UNK_008 = 1 << 3,
    IS_LADDER_016 = 1 << 4,
    UNK_032 = 1 << 5,
    UNK_064 = 1 << 6,
    IS_WALL_CLIMBING_MAYBE_128 = 1 << 7, // set while climbing?
    UNK_256 = 1 << 8,
    UNK_512 = 1 << 9,
    SWIMMING_1024 = 1 << 10,
    UNK_2048 = 1 << 11,
    UNK_4096 = 1 << 12,
    UNK_8192 = 1 << 13,
    UNK_16384 = 1 << 14,
    UNK_32768 = 1 << 15,
    UNK_65536 = 1 << 16,
    UNK_131072 = 1 << 17,
    UNK_262144 = 1 << 18,
    UNK_524288 = 1 << 19,
    UNK_1048576 = 1 << 20,
    UNK_2097152 = 1 << 21,
    UNK_4194304 = 1 << 22,
    UNK_8388608 = 1 << 23,
    UNK_16777216 = 1 << 24,
    UNK_33554432 = 1 << 25,
    UNK_67108864 = 1 << 26,
    EVENT_UNK_134217728 = 1 << 27, // GETS DISABLED WHEN INSIDE AN EVENT
    EVENT2_UNK_268435456 = 1 << 28,
    UNK_536870912 = 1 << 29,
    UNK_1073741824 = 1 << 30,
    GET_HIT_MAYBE_UNABLE_TO_INTERACT = 1u << 31
};

struct PlayerBase : PlayerOrEnemy {
    PADDED_BYTES(0x838, 0x8D8);
    BEType<PlayerMoveBitFlags> moveBitFlags;
    PADDED_BYTES(0x8E0, 0x12A4);
};
static_assert(offsetof(PlayerBase, moveBitFlags) == 0x8DC, "Player.float834 offset mismatch");
static_assert(sizeof(PlayerBase) == 0x12A8, "PlayerBase size mismatch");

struct Player : PlayerBase {
    PADDED_BYTES(0x12A8, 0x2524);
};
static_assert(sizeof(Player) == 0x2528, "Player size mismatch");

struct WeaponBase : ActorWiiU {
    PADDED_BYTES(0x53C, 0x5F0);
    BEType<uint32_t> field_5F4;
    BEType<uint8_t> isEquippedProbably;
    BEType<uint8_t> field_5FD;
    BEType<uint8_t> field_5FE;
    BEType<uint8_t> field_5FF;
    PADDED_BYTES(0x600, 0x72C);
};
static_assert(offsetof(WeaponBase, isEquippedProbably) == 0x5F8, "WeaponBase.isEquippedProbably offset mismatch");
static_assert(sizeof(WeaponBase) == 0x72C, "WeaponBase size mismatch");

struct Struct20 {
    BEType<uint32_t> __vftable;
    BEType<uint32_t> field_4;
    BEType<uint32_t> field_8;
    BEType<uint32_t> field_C;
    BEType<uint32_t> field_10;
    BEType<uint32_t> field_14;
};

struct DamageMgr {
    BEType<uint32_t> struct20Ptr;
    BEType<uint32_t> struct20PlayerRelatedPtr;
    BEType<uint32_t> actor;
    struct {
        BEType<uint32_t> size;
        BEType<uint32_t> pointer;
    } damageCallbacks;
    BEType<uint32_t> field_14;
    BEType<uint8_t> field_18;
    BEType<uint8_t> field_19;
    BEType<uint8_t> field_1A;
    BEType<uint8_t> field_1B;
    BEType<uint32_t> __vftable;
    BEType<uint32_t> deleter;
    BEType<uint32_t> field_24;
    BEType<uint32_t> damage;
    BEType<uint32_t> field_2C;
    BEType<uint32_t> minDmg;
    BEType<uint32_t> field_34;
    BEType<uint32_t> field_38;
    BEType<uint32_t> field_3C;
    BEType<uint32_t> damageType;
    BEType<uint32_t> field_44;
    BEType<uint8_t> field_48;
    BEType<uint8_t> field_49;
    BEType<uint8_t> field_4A;
    BEType<uint8_t> field_4B;
};
static_assert(sizeof(DamageMgr) == 0x4C, "DamageMgr size mismatch");

struct AttackSensorInitArg {
    BEType<uint32_t> mode;
    BEType<uint32_t> flags;
    BEType<float> multiplier;
    BEType<float> scale;
    BEType<uint32_t> shieldBreakPower;
    BEType<uint8_t> overrideImpact;
    BEType<uint8_t> unk_15;
    BEType<uint8_t> unk_16;
    BEType<uint8_t> unk_17;
    BEType<uint32_t> powerForPlayers;
    BEType<uint32_t> impact;
    BEType<uint32_t> unk_20;
    BEType<uint32_t> comboCount;
    BEType<uint8_t> isContactLayerInitialized;
    BEType<uint8_t> field_87C;
    BEType<uint8_t> field_87D;
    BEType<uint8_t> field_87E;
    BEType<uint8_t> resetAttack;
    BEType<uint8_t> field_8A2;
    BEType<uint8_t> field_8A3;
    BEType<uint8_t> field_8A4;
};
static_assert(sizeof(AttackSensorInitArg) == 0x30, "AttackSensorInitArg size mismatch");

struct AttackSensorOtherArg {
    BEType<uint32_t> flags;
    BEType<uint32_t> shieldBreakPower;
    BEType<uint8_t> overrideImpact;
    BEType<uint8_t> field_09;
    BEType<uint8_t> field_0A;
    BEType<uint8_t> field_0B;
    BEType<float> scale;
    BEType<float> multiplier;
    BEType<uint8_t> resetAttack;
    BEType<uint8_t> wasAttackMode;
    BEType<uint8_t> field_12;
    BEType<uint8_t> field_13;
    BEType<uint32_t> powerForPlayers;
    BEType<uint32_t> impact;
    BEType<uint32_t> comboCount;
};
static_assert(sizeof(AttackSensorOtherArg) == 0x24, "AttackSensorOtherArg size mismatch");

enum WeaponType : uint32_t {
    SmallSword = 0x0,
    LargeSword = 0x1,
    Spear = 0x2,
    Bow = 0x3,
    Shield = 0x4,
    UnknownWeapon = 0x5,
};

struct Weapon : WeaponBase {
    PADDED_BYTES(0x72C, 0x870);
    AttackSensorInitArg setupAttackSensor;
    PADDED_BYTES(0x8A4, 0x934);
    BEType<WeaponType> type;
    AttackSensorOtherArg finalizedAttackSensor;
    BEType<uint32_t> weaponShockwaves;
    PADDED_BYTES(0x964, 0x96C);
    BEType<uint32_t> field_974_triggerEventsMaybe;
    BEVec3 field_978;
    BEType<uint32_t> field_984;
    BEType<uint32_t> field_988;
    BEType<uint32_t> field_98C;
    BEType<uint32_t> field_990;
    BEVec3 originalScale;
    BEType<uint32_t> field_99C;
    BEType<uint32_t> field_9A0;
    BEType<uint32_t> field_9A4;
    BEType<uint32_t> field_9A8;
    BEType<uint32_t> field_9AC;
    BEType<uint32_t> field_9B0;
    BEType<uint32_t> field_9B4;
    struct {
        PADDED_BYTES(0x00, 0x08);
        BEType<uint32_t> struct7Ptr; // stores attackinfo, so prolly post-hit stuff
        BEType<uint32_t> field_10;
        BEType<uint32_t> field_14;
        BEType<uint32_t> field_18;
        BEType<uint32_t> field_1C;
        BEType<uint32_t> attackSensorPtr;
        BEType<uint32_t> struct8Ptr; // stores attackinfo, so prolly post-hit stuff. seems to be more player specific
        BEType<uint32_t> field_28;
        BEType<uint32_t> field_2C;
        BEType<uint32_t> field_30;
        BEType<uint32_t> field_34;
        BEType<uint32_t> attackSensorStruct8Ptr;
        BEType<uint8_t> field_3C;
        PADDED_BYTES(0x3D, 0x3C);
    } actorAtk;
    BEType<float> field_9F8;
    BEType<float> field_9FC;
    BEType<uint32_t> damageMgrPtr;
    PADDED_BYTES(0xA04, 0xA10);
    BEType<uint16_t> weaponFlags;
    BEType<uint16_t> otherFlags;
    PADDED_BYTES(0xA18, 0xB58);
};
static_assert(offsetof(Weapon, setupAttackSensor.resetAttack) == 0x8A0, "Weapon.setupAttackSensor.resetAttack offset mismatch");
static_assert(offsetof(Weapon, setupAttackSensor.mode) == 0x874, "Weapon.setupAttackSensor.mode offset mismatch");
static_assert(offsetof(Weapon, finalizedAttackSensor.resetAttack) == 0x950, "Weapon.finalizedAttackSensor.resetAttack offset mismatch");
static_assert(sizeof(Weapon) == 0xB5C, "Weapon size mismatch");

struct LookAtMatrix {
    BEVec3 pos;
    BEVec3 target;
    BEVec3 up;
    BEVec3 unknown;
    BEType<float> zNear;
    BEType<float> zFar;
};

struct ActCamera : ActorWiiU {
    BEType<uint32_t> dword53C;
    BEType<float> float540;
    PADDED_BYTES(0x544, 0x54C);
    LookAtMatrix origCamMtx;
    PADDED_BYTES(0x588, 0x5BC);
    LookAtMatrix finalCamMtx;
};
static_assert(offsetof(ActCamera, origCamMtx) == 0x550, "ActCamera.origCamMtx offset mismatch");
static_assert(offsetof(ActCamera, finalCamMtx) == 0x5C0, "ActCamera.finalCamMtx offset mismatch");

#pragma pack(pop)

inline std::string contactLayerNames[] = {
    "EntityObject",
    "EntitySmallObject",
    "EntityGroundObject",
    "EntityPlayer",
    "EntityNPC",
    "EntityRagdoll",
    "EntityWater",
    "EntityAirWall",
    "EntityGround",
    "EntityGroundSmooth",
    "EntityGroundRough",
    "EntityRope",
    "EntityTree",
    "EntityNPC_NoHitPlayer",
    "EntityHitOnlyWater",
    "EntityWallForClimb",
    "EntityHitOnlyGround",
    "EntityQueryCustomReceiver",
    "EntityForbidden18",
    "EntityNoHit",
    "EntityMeshVisualizer",
    "EntityForbidden21",
    "EntityForbidden22",
    "EntityForbidden23",
    "EntityForbidden24",
    "EntityForbidden25",
    "EntityForbidden26",
    "EntityForbidden27",
    "EntityForbidden28",
    "EntityForbidden29",
    "EntityForbidden30",
    "EntityEnd",

    "SensorObject",
    "SensorSmallObject",
    "SensorPlayer",
    "SensorEnemy",
    "SensorNPC",
    "SensorHorse",
    "SensorRope",
    "SensorAttackPlayer",
    "SensorAttackEnemy",
    "SensorChemical",
    "SensorTerror",
    "SensorHitOnlyInDoor",
    "SensorInDoor",
    "SensorReserve13",
    "SensorReserve14",
    "SensorChemicalElement",
    "SensorAttackCommon",
    "SensorQueryOnly",
    "SensorTree",
    "SensorCamera",
    "SensorMeshVisualizer",
    "SensorNoHit",
    "SensorReserve20",
    "SensorCustomReceiver",
    "SensorEnd"
};

enum class ContactLayer : uint32_t {
    EntityObject = 0,
    EntitySmallObject,
    EntityGroundObject,
    EntityPlayer,
    EntityNPC,
    EntityRagdoll,
    EntityWater,
    EntityAirWall,
    EntityGround,
    EntityGroundSmooth,
    EntityGroundRough,
    EntityRope,
    EntityTree,
    EntityNPC_NoHitPlayer,
    EntityHitOnlyWater,
    EntityWallForClimb,
    EntityHitOnlyGround,
    EntityQueryCustomReceiver,
    EntityForbidden18,
    EntityNoHit,
    EntityMeshVisualizer,
    EntityForbidden21,
    EntityForbidden22,
    EntityForbidden23,
    EntityForbidden24,
    EntityForbidden25,
    EntityForbidden26,
    EntityForbidden27,
    EntityForbidden28,
    EntityForbidden29,
    EntityForbidden30,
    EntityEnd,

    SensorObject,
    SensorSmallObject,
    SensorPlayer,
    SensorEnemy,
    SensorNPC,
    SensorHorse,
    SensorRope,
    SensorAttackPlayer,
    SensorAttackEnemy,
    SensorChemical,
    SensorTerror,
    SensorHitOnlyInDoor,
    SensorInDoor,
    SensorReserve13,
    SensorReserve14,
    SensorChemicalElement,
    SensorAttackCommon,
    SensorQueryOnly,
    SensorTree,
    SensorCamera,
    SensorMeshVisualizer,
    SensorNoHit,
    SensorReserve20,
    SensorCustomReceiver,
    SensorEnd
};