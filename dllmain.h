#pragma once
#include "stdafx.h"

#define BONE_HEAD_ID (66)

namespace Minipulation {
	extern bool NoSpread;
	extern PVOID LocalPlayerPawn;
	extern PVOID LocalPlayerController;
	extern PVOID TargetPawn;
	extern PVOID boatPawns;
	extern PVOID TargetBoat;
	extern PVOID(*ProcessEvent)(PVOID, PVOID, PVOID, PVOID);
	BOOLEAN GetTargetHead(FVector& out);
	extern bool debugshitaf3ai;
	extern float x;
	extern float y;
	extern float z;
	extern float fara;
	extern float neara;
	extern float widtha;
}

namespace Offsets {
	typedef struct {
		LPCWSTR Name;
		DWORD& Offset;
	} OFFSET;

	extern PVOID* uWorld;
	extern PVOID LaunchCharacter;
	namespace Engine {
		namespace World {
			extern DWORD OwningGameInstance;
			extern DWORD Levels;
		}

		namespace Level {
			extern DWORD AActors;
		}

		namespace GameInstance {
			extern DWORD LocalPlayers;
		}

		namespace Player {
			extern DWORD PlayerController;
		}

		namespace Controller {
			extern DWORD ControlRotation;
			extern PVOID SetControlRotation;
		}

		namespace PlayerController { //PlayerCameraManager
			extern DWORD AcknowledgedPawn;
			extern DWORD PlayerCameraManager;
		}

		namespace Pawn {
			extern DWORD PlayerState;
		}

		namespace PlayerState {
			extern PVOID GetPlayerName;
		}

		namespace Actor {
			extern DWORD RootComponent;
		}

		namespace Character {
			extern DWORD Mesh;
		}

		namespace SceneComponent {
			extern DWORD RelativeLocation;
			extern DWORD ComponentVelocity;
		}

		namespace StaticMeshComponent {
			extern DWORD ComponentToWorld;
			extern DWORD StaticMesh;
		}

		namespace SkinnedMeshComponent {
			extern DWORD CachedWorldSpaceBounds;
		}
	}

	namespace FortniteGame {
		namespace FortPawn {
			extern DWORD bIsDBNO;
			extern DWORD bIsDying;
			extern DWORD CurrentWeapon;
		}

		namespace FortPickup {
			extern DWORD PrimaryPickupItemEntry;
		}

		namespace FortItemEntry {
			extern DWORD ItemDefinition;
		}

		namespace FortItemDefinition {
			extern DWORD DisplayName;
			extern DWORD Tier;
		}

		namespace FortPlayerStateAthena {
			extern DWORD TeamIndex;
		}

		namespace FortWeapon {
			extern DWORD WeaponData;
		}

		namespace FortWeaponItemDefinition {
			extern DWORD WeaponStatHandle;
		}

		namespace FortProjectileAthena {
			extern DWORD FireStartLoc;
		}

		namespace FortBaseWeaponStats {
			extern DWORD ReloadTime;
		}

		namespace BuildingContainer {
			extern DWORD bAlreadySearched;
		}
	}
}

#pragma once
#include "stdafx.h"
#define PI (3.141592653589793f)
#define RELATIVE_ADDR(addr, size) ((PBYTE)((UINT_PTR)(addr) + *(PINT)((UINT_PTR)(addr) + ((size) - sizeof(INT))) + (size)))

#define ReadPointer(base, offset) (*(PVOID *)(((PBYTE)base + offset)))
#define ReadDWORD(base, offset) (*(PDWORD)(((PBYTE)base + offset)))
#define ReadBYTE(base, offset) (*(((PBYTE)base + offset)))

namespace Utilities {
	VOID CreateConsole();
	PBYTE FindPattern(LPCSTR pattern, LPCSTR mask);
	std::wstring GetObjectFirstName(UObject* object);
	std::wstring GetObjectName(UObject* object);
	PVOID FindObject(LPCWSTR name);
	BOOLEAN WorldToScreen(float width, float height, float inOutPosition[3]);
	VOID ToMatrixWithScale(float* in, float out[4][4]);
	VOID GetBoneLocation(float compMatrix[4][4], PVOID bones, DWORD index, float out[3]);
	BOOLEAN LineOfSightTo(PVOID PlayerController, PVOID Actor, FVector* ViewPoint);
	FMinimalViewInfo& GetViewInfo();
	FVector* GetPawnRootLocation(PVOID pawn);
	VOID CalcAngle(float* src, float* dst, float* angles);

	extern VOID(*FreeInternal)(PVOID);

	namespace _SpoofCallInternal {
		extern "C" PVOID RetSpoofStub();

		template <typename Ret, typename... Args>
		inline Ret Wrapper(PVOID shell, Args... args) {
			auto fn = (Ret(*)(Args...))(shell);
			return fn(args...);
		}

		template <std::size_t Argc, typename>
		struct Remapper {
			template<typename Ret, typename First, typename Second, typename Third, typename Fourth, typename... Pack>
			static Ret Call(PVOID shell, PVOID shell_param, First first, Second second, Third third, Fourth fourth, Pack... pack) {
				return Wrapper<Ret, First, Second, Third, Fourth, PVOID, PVOID, Pack...>(shell, first, second, third, fourth, shell_param, nullptr, pack...);
			}
		};

		template <std::size_t Argc>
		struct Remapper<Argc, std::enable_if_t<Argc <= 4>> {
			template<typename Ret, typename First = PVOID, typename Second = PVOID, typename Third = PVOID, typename Fourth = PVOID>
			static Ret Call(PVOID shell, PVOID shell_param, First first = First{}, Second second = Second{}, Third third = Third{}, Fourth fourth = Fourth{}) {
				return Wrapper<Ret, First, Second, Third, Fourth, PVOID, PVOID>(shell, first, second, third, fourth, shell_param, nullptr);
			}
		};
	}

	template <typename Ret, typename... Args>
	Ret SpoofCall(Ret(*fn)(Args...), Args... args) {
		static PVOID trampoline = nullptr;
		if (!trampoline) {
			trampoline = Utilities::FindPattern("\xFF\x27", "xx");
			if (!trampoline) {
				MessageBox(0, L"Failed to find valid trampoline", L"Failure", 0);
				ExitProcess(0);
			}
		}

		struct {
			PVOID Trampoline;
			PVOID Function;
			PVOID Reg;
		} params = {
			trampoline,
			reinterpret_cast<void*>(fn),
		};

		return _SpoofCallInternal::Remapper<sizeof...(Args), void>::template Call<Ret, Args...>(&_SpoofCallInternal::RetSpoofStub, &params, args...);
	}
}

typedef struct {
	bool Aimbot;
	bool AutoAimbot;
	bool SilentAimbot;
	bool NoSpreadAimbot;
	float AimbotFOV;
	float AimbotSlow;
	bool InstantReload;
	float FOV;
	std::string AimKey;
	std::string aimpart;
	bool bullettp;
	bool RocketTp;
	bool Spinbot;
	bool SpeedHack;
	bool Airstuck;
	bool BoatTeleport;
	bool Boatfly;
	bool Rapidfire;
	bool fovslider;
	bool weakpointaim;
	bool wkekj;
	float CamSpeed;
	float CornerSize;
	bool Freecam;

	struct {
		bool AimbotFOV;
		bool Players;
		bool Radar;
		bool skeleton;
		bool DBox;
		bool Boat;
		bool crosshair;
		std::string BoxType;
		std::string SnaplineLocation;
		bool box;
		float ChestDistance;
		bool Helicopter;
		bool PlayerLines;
		bool PlayerNames;
		float PlayerVisibleColor[3];
		float PlayerNotVisibleColor[3];
		bool Ammo;
		bool Containers;
		bool Weapons;
		bool Box;
		INT MinWeaponTier;
	} ESP;
} OPTIONS;
struct {
	//keybind

} keybind;

extern OPTIONS options;

namespace optionsHelper {
	VOID Initialize();
	VOID Saveoptions();
}