#include "stdafx.h"
#include "dllmain.h"
#include <locale>
#include <iostream>
#include "Discord.h"
#include "FortUpdater.h"
#include "Helper.h"

WNDPROC oWndProc;
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool ShowMenu = false;

const char* items[] = { "  Head", "  Chest", "  Leg", "  Pelvis" };
const char* current_item = "  Head";

const char* aimmodes[] = { "  None", "  Memory", "  Silent" };
const char* current_aimmode = "  Memory";

const char* aimkeys[] = { "  Left Click", "  Right Click", "  Middle Click", "  CAPS", "  Left Shift", "  Alt", "  Q", "  Z", "  X", "  C", "  `" };
const char* current_aimkey = "  Right Click";

const char* snaplinelocations[] = { "  Top", "  Centre", "  Bottom" };
const char* current_snaplinelocation = "Top";

const char* boxtypes[] = { "  Cornered", "  Cornered Fill", "  Box", "	BoxA" };
const char* current_boxtype = "  Box";

OPTIONS options = { 0 };

namespace Utilities {
	GObjects* objects = nullptr;
	FString(*GetObjectNameInternal)(PVOID) = nullptr;
	VOID(*FreeInternal)(PVOID) = nullptr;
	BOOL(*LineOfSightToInternal)(PVOID PlayerController, PVOID Actor, FVector* ViewPoint) = nullptr;
	VOID(*CalculateProjectionMatrixGivenView)(FMinimalViewInfo* viewInfo, BYTE aspectRatioAxisConstraint, PBYTE viewport, FSceneViewProjectionData* inOutProjectionData) = nullptr;

	struct {
		FMinimalViewInfo Info;
		float ProjectionMatrix[4][4];
	} view = { 0 };

	VOID CreateConsole() {
		AllocConsole();
		static_cast<VOID>(freopen("CONIN$", "r", stdin));
		static_cast<VOID>(freopen("CONOUT$", "w", stdout));
		static_cast<VOID>(freopen("CONOUT$", "w", stderr));
	}

	BOOLEAN MaskCompare(PVOID buffer, LPCSTR pattern, LPCSTR mask) {
		for (auto b = reinterpret_cast<PBYTE>(buffer); *mask; ++pattern, ++mask, ++b) {
			if (*mask == 'x' && *reinterpret_cast<LPCBYTE>(pattern) != *b) {
				return FALSE;
			}
		}

		return TRUE;
	}

	PBYTE FindPattern(PVOID base, DWORD size, LPCSTR pattern, LPCSTR mask) {
		size -= static_cast<DWORD>(strlen(mask));

		for (auto i = 0UL; i < size; ++i) {
			auto addr = reinterpret_cast<PBYTE>(base) + i;
			if (MaskCompare(addr, pattern, mask)) {
				return addr;
			}
		}

		return NULL;
	}
	PBYTE FindPattern(LPCSTR pattern, LPCSTR mask) {
		MODULEINFO info = { 0 };
		GetModuleInformation(GetCurrentProcess(), GetModuleHandle(0), &info, sizeof(info));

		return FindPattern(info.lpBaseOfDll, info.SizeOfImage, pattern, mask);
	}

	VOID Free(PVOID buffer) {
		FreeInternal(buffer);
	}

	std::wstring GetObjectFirstName(UObject* object) {
		auto internalName = GetObjectNameInternal(object);
		if (!internalName.c_str()) {
			return L"";
		}

		std::wstring name(internalName.c_str());
		Free(internalName.c_str());

		return name;
	}

	std::wstring GetObjectName(UObject* object) {
		std::wstring name(L"");
		for (auto i = 0; object; object = object->Outer, ++i) {
			auto internalName = GetObjectNameInternal(object);
			if (!internalName.c_str()) {
				break;
			}

			name = internalName.c_str() + std::wstring(i > 0 ? L"." : L"") + name;
			Free(internalName.c_str());
		}

		return name;
	}

	BOOLEAN GetOffsets(std::vector<Offsets::OFFSET>& offsets) {
		auto current = 0ULL;
		auto size = offsets.size();

		for (auto array : objects->ObjectArray->Objects) {
			auto fuObject = array;
			for (auto i = 0; i < 0x10000 && fuObject->Object; ++i, ++fuObject) {
				auto object = fuObject->Object;
				if (object->ObjectFlags != 0x41) {
					continue;
				}

				auto name = GetObjectName(object);
				for (auto& o : offsets) {
					if (!o.Offset && name == o.Name) {
						o.Offset = *reinterpret_cast<PDWORD>(reinterpret_cast<PBYTE>(object) + 0x44);

						if (++current == size) {
							return TRUE;
						}

						break;
					}
				}
			}
		}

		for (auto& o : offsets) {
			if (!o.Offset) {
				WCHAR buffer[0xFF] = { 0 };
				wsprintf(buffer, L"Offset %ws not found", o.Name);
				MessageBox(0, buffer, L"Failure", 0);
			}
		}

		return FALSE;
	}

	PVOID FindObject(LPCWSTR name) {
		for (auto array : objects->ObjectArray->Objects) {
			auto fuObject = array;
			for (auto i = 0; i < 0x10000 && fuObject->Object; ++i, ++fuObject) {
				auto object = fuObject->Object;
				if (object->ObjectFlags != 0x41) {
					continue;
				}

				if (GetObjectName(object) == name) {
					return object;
				}
			}
		}

		return 0;
	}

	VOID ToMatrixWithScale(float* in, float out[4][4])
	{
		auto* rotation = &in[0];
		auto* translation = &in[4];
		auto* scale = &in[8];

		out[3][0] = translation[0];
		out[3][1] = translation[1];
		out[3][2] = translation[2];

		auto x2 = rotation[0] + rotation[0];
		auto y2 = rotation[1] + rotation[1];
		auto z2 = rotation[2] + rotation[2];

		auto xx2 = rotation[0] * x2;
		auto yy2 = rotation[1] * y2;
		auto zz2 = rotation[2] * z2;
		out[0][0] = (1.0f - (yy2 + zz2)) * scale[0];
		out[1][1] = (1.0f - (xx2 + zz2)) * scale[1];
		out[2][2] = (1.0f - (xx2 + yy2)) * scale[2];

		auto yz2 = rotation[1] * z2;
		auto wx2 = rotation[3] * x2;
		out[2][1] = (yz2 - wx2) * scale[2];
		out[1][2] = (yz2 + wx2) * scale[1];

		auto xy2 = rotation[0] * y2;
		auto wz2 = rotation[3] * z2;
		out[1][0] = (xy2 - wz2) * scale[1];
		out[0][1] = (xy2 + wz2) * scale[0];

		auto xz2 = rotation[0] * z2;
		auto wy2 = rotation[3] * y2;
		out[2][0] = (xz2 + wy2) * scale[2];
		out[0][2] = (xz2 - wy2) * scale[0];

		out[0][3] = 0.0f;
		out[1][3] = 0.0f;
		out[2][3] = 0.0f;
		out[3][3] = 1.0f;
	}

	VOID MultiplyMatrices(float a[4][4], float b[4][4], float out[4][4]) {
		for (auto r = 0; r < 4; ++r) {
			for (auto c = 0; c < 4; ++c) {
				auto sum = 0.0f;

				for (auto i = 0; i < 4; ++i) {
					sum += a[r][i] * b[i][c];
				}

				out[r][c] = sum;
			}
		}
	}

	VOID GetBoneLocation(float compMatrix[4][4], PVOID bones, DWORD index, float out[3]) {
		float boneMatrix[4][4];
		ToMatrixWithScale((float*)((PBYTE)bones + (index * 0x30)), boneMatrix);

		float result[4][4];
		MultiplyMatrices(boneMatrix, compMatrix, result);

		out[0] = result[3][0];
		out[1] = result[3][1];
		out[2] = result[3][2];
	}

	VOID GetViewProjectionMatrix(FSceneViewProjectionData* projectionData, float out[4][4]) {
		auto loc = &projectionData->ViewOrigin;

		float translation[4][4] = {
			{ 1.0f, 0.0f, 0.0f, 0.0f, },
			{ 0.0f, 1.0f, 0.0f, 0.0f, },
			{ 0.0f, 0.0f, 1.0f, 0.0f, },
			{ -loc->X, -loc->Y, -loc->Z, 0.0f, },
		};

		float temp[4][4];
		MultiplyMatrices(translation, projectionData->ViewRotationMatrix.M, temp);
		MultiplyMatrices(temp, projectionData->ProjectionMatrix.M, out);
	}

	BOOLEAN ProjectWorldToScreen(float viewProjection[4][4], float width, float height, float inOutPosition[3]) {
		float res[4] = {
			viewProjection[0][0] * inOutPosition[0] + viewProjection[1][0] * inOutPosition[1] + viewProjection[2][0] * inOutPosition[2] + viewProjection[3][0],
			viewProjection[0][1] * inOutPosition[0] + viewProjection[1][1] * inOutPosition[1] + viewProjection[2][1] * inOutPosition[2] + viewProjection[3][1],
			viewProjection[0][2] * inOutPosition[0] + viewProjection[1][2] * inOutPosition[1] + viewProjection[2][2] * inOutPosition[2] + viewProjection[3][2],
			viewProjection[0][3] * inOutPosition[0] + viewProjection[1][3] * inOutPosition[1] + viewProjection[2][3] * inOutPosition[2] + viewProjection[3][3],
		};

		auto r = res[3];
		if (r > 0) {
			auto rhw = 1.0f / r;

			inOutPosition[0] = (((res[0] * rhw) / 2.0f) + 0.5f) * width;
			inOutPosition[1] = (0.5f - ((res[1] * rhw) / 2.0f)) * height;
			inOutPosition[2] = r;

			return TRUE;
		}

		return FALSE;
	}

	VOID CalculateProjectionMatrixGivenViewHook(FMinimalViewInfo* viewInfo, BYTE aspectRatioAxisConstraint, PBYTE viewport, FSceneViewProjectionData* inOutProjectionData) {
		CalculateProjectionMatrixGivenView(viewInfo, aspectRatioAxisConstraint, viewport, inOutProjectionData);

		view.Info = *viewInfo;
		GetViewProjectionMatrix(inOutProjectionData, view.ProjectionMatrix);
	}

	BOOLEAN WorldToScreen(float width, float height, float inOutPosition[3]) {
		return ProjectWorldToScreen(view.ProjectionMatrix, width, height, inOutPosition);
	}

	BOOLEAN LineOfSightTo(PVOID PlayerController, PVOID Actor, FVector* ViewPoint) {
		return SpoofCall(LineOfSightToInternal, PlayerController, Actor, ViewPoint);
	}

	FMinimalViewInfo& GetViewInfo() {
		return view.Info;
	}

	FVector* GetPawnRootLocation(PVOID pawn) {
		auto root = ReadPointer(pawn, Offsets::Engine::Actor::RootComponent);
		if (!root) {
			return nullptr;
		}

		return reinterpret_cast<FVector*>(reinterpret_cast<PBYTE>(root) + Offsets::Engine::SceneComponent::RelativeLocation);
	}

	float Normalize(float angle) {
		float a = (float)fmod(fmod(angle, 360.0) + 360.0, 360.0);
		if (a > 180.0f) {
			a -= 360.0f;
		}
		return a;
	}

	VOID CalcAngle(float* src, float* dst, float* angles) {
		float rel[3] = {
			dst[0] - src[0],
			dst[1] - src[1],
			dst[2] - src[2],
		};

		auto dist = sqrtf(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
		auto yaw = atan2f(rel[1], rel[0]) * (180.0f / PI);
		auto pitch = (-((acosf((rel[2] / dist)) * 180.0f / PI) - 90.0f));

		angles[0] = Normalize(pitch);
		angles[1] = Normalize(yaw);
	}
}

namespace optionsHelper {
	VOID Saveoptions() {
		CHAR path[0xFF];
		GetTempPathA(sizeof(path) / sizeof(path[0]), path);
		strcat(path, ("fnambt.options"));

		auto file = fopen(path, ("wb"));
		if (file) {
			fwrite(&options, sizeof(options), 1, file);
			fclose(file);
		}
	}

	VOID Resetoptions() {
		options = { 0 };
		options.CamSpeed = 10.0f;
		options.Freecam = false;
		options.Aimbot = true;
		options.RocketTp = false;
		options.Spinbot = false;
		options.bullettp = false;
		options.Airstuck = false;
		options.AutoAimbot = false;
		options.NoSpreadAimbot = false;
		options.fovslider = false;
		options.AimbotFOV = 100.0f;
		options.ESP.PlayerLines = false;
		options.AimbotSlow = 0.0f;
		options.InstantReload = false;
		options.FOV = 120.0f;
		options.ESP.AimbotFOV = true;
		options.ESP.Players = true;
		options.AimKey = "VK_RBUTTON";
		options.aimpart = "Head";
		options.ESP.BoxType = "Box";
		options.ESP.SnaplineLocation = "- 1080";
		options.CornerSize = 7.0f;
		options.weakpointaim = true;
		options.ESP.crosshair = true;
		options.ESP.box = true;
		options.ESP.PlayerNames = true;
		options.ESP.DBox = false;
		options.ESP.skeleton = true;
		options.ESP.Boat = false;
		options.ESP.Helicopter = false;
		options.wkekj = true;
		options.ESP.Radar = true;
		options.ESP.PlayerVisibleColor[0] = 1.0f;
		options.ESP.PlayerVisibleColor[1] = 0.0f;
		options.ESP.PlayerVisibleColor[2] = 0.0f;
		options.ESP.PlayerNotVisibleColor[0] = 1.0f;
		options.ESP.PlayerNotVisibleColor[1] = 0.08f;
		options.ESP.PlayerNotVisibleColor[2] = 0.6f;
		options.ESP.Containers = true;
		options.ESP.Weapons = true;
		options.ESP.MinWeaponTier = 2;

		Saveoptions();
	}

	VOID Initialize() {
		CHAR path[0xFF] = { 0 };
		GetTempPathA(sizeof(path) / sizeof(path[0]), path);
		strcat(path, ("fnambt.options"));

		auto file = fopen(path, ("rb"));
		if (file) {
			fseek(file, 0, SEEK_END);
			auto size = ftell(file);

			if (size == sizeof(options)) {
				fseek(file, 0, SEEK_SET);
				fread(&options, sizeof(options), 1, file);
				fclose(file);
			}
			else {
				fclose(file);
				Resetoptions();
			}
		}
		else {
			Resetoptions();
		}
	}
}

namespace Offsets {
	PVOID* uWorld = 0;
	PVOID LaunchCharacter = 0;
	namespace Engine {
		namespace World {
			DWORD OwningGameInstance = 0x188;
			DWORD Levels = 0x148;
		}

		namespace Level {
			DWORD AActors = 0x98;
		}

		namespace GameInstance {
			DWORD LocalPlayers = 0x38;
		}

		namespace Player {
			DWORD PlayerController = 0x30;
		}

		namespace Controller {
			DWORD ControlRotation = 0x280;
			PVOID SetControlRotation = 0;
		}

		namespace PlayerController {
			DWORD AcknowledgedPawn = 0x298;
			DWORD PlayerCameraManager = 0x2B0;
		}

		namespace Pawn {
			DWORD PlayerState = 0x238;
		}

		namespace PlayerState {
			PVOID GetPlayerName = 0;
		}

		namespace Actor {
			DWORD RootComponent = 0x130;
		}

		namespace Character {
			DWORD Mesh = 0x278;
		}

		namespace SceneComponent {
			DWORD RelativeLocation = 0x11C;
			DWORD ComponentVelocity = 0x140;
		}

		namespace StaticMeshComponent {
			DWORD ComponentToWorld = 0x1C0;
			DWORD StaticMesh = 0x420;
		}

		namespace SkinnedMeshComponent {
			DWORD CachedWorldSpaceBounds = 0x5A0;
		}
	}

	namespace FortniteGame {
		namespace FortPawn {
			DWORD bIsDBNO = 0x53A;
			DWORD bIsDying = 0x520;
			DWORD CurrentWeapon = 0x588;
		}

		namespace FortPickup {
			DWORD PrimaryPickupItemEntry = 0x290;
		}

		namespace FortItemEntry {
			DWORD ItemDefinition = 0x18;
		}

		namespace FortItemDefinition {
			DWORD DisplayName = 0x70;
			DWORD Tier = 0x54;
		}

		namespace FortPlayerStateAthena {
			DWORD TeamIndex = 0xE68;
		}

		namespace FortWeapon {
			DWORD WeaponData = 0x358;
		}

		namespace FortWeaponItemDefinition {
			DWORD WeaponStatHandle = 0x7B8;
		}

		namespace FortProjectileAthena {
			DWORD FireStartLoc = 0x868;
		}

		namespace FortBaseWeaponStats {
			DWORD ReloadTime = 0xFC;
		}

		namespace BuildingContainer {
			DWORD bAlreadySearched = 0xC51;
		}
	}

	namespace UI {
		namespace ItemCount {
			DWORD ItemDefinition = 0;
		}
	}
}

VOID AddMarker(ImGuiWindow& window, float width, float height, float* start, PVOID pawn, LPCSTR text, ImU32 color) {
	auto root = Utilities::GetPawnRootLocation(pawn);
	if (root) {
		auto pos = *root;
		float dx = start[0] - pos.X;
		float dy = start[1] - pos.Y;
		float dz = start[2] - pos.Z;

		if (Utilities::WorldToScreen(width, height, &pos.X)) {
			float dist = Utilities::SpoofCall(sqrtf, dx * dx + dy * dy + dz * dz) / 100.0f;

			CHAR modified[0xFF] = { 0 };
			snprintf(modified, sizeof(modified), ("%s {%dm}"), text, static_cast<INT>(dist));

			auto size = ImGui::GetFont()->CalcTextSizeA(window.DrawList->_Data->FontSize, FLT_MAX, 0, modified);
			window.DrawList->AddText(ImVec2(pos.X - size.x / 2.0f, pos.Y - size.y / 2.0f), color, modified);
		}
	}
}


__declspec(dllexport) LRESULT CALLBACK Mouse(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_KEYUP && (wParam == VK_INSERT || (ShowMenu && wParam == VK_ESCAPE))) {
		ShowMenu = !ShowMenu;
		ImGui::GetIO().MouseDrawCursor = ShowMenu;

	}
	else if (msg == WM_QUIT && ShowMenu) {
		ExitProcess(0);
	}

	if (ShowMenu) {
		ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
		return TRUE;
	}

	return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

extern uint64_t base_address = 0;
DWORD processID;
const ImVec4 color = { 255.0,255.0,255.0,1 };
const ImVec4 black = { 0.0,0,0,0 };
const ImVec4 red = { 0.65,0,0,1 };
const ImVec4 white = { 255.0,255.0,255.0,1 };
const ImVec4 green = { 0.03,0.81,0.14,1 };
const ImVec4 blue = { 0.21960784313,0.56470588235,0.90980392156,1.0 };

ImGuiWindow& BeginScene() {
	ImGui_ImplDX11_NewFrame();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
	ImGui::Begin(("##scene"), nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);
	auto& io = ImGui::GetIO();
	ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGuiCond_Always);

	return *ImGui::GetCurrentWindow();
}

VOID EndScene(ImGuiWindow& window) {
	window.DrawList->PushClipRectFullScreen();
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	ImGui::Render();
}


inline std::wstring to_wstring(const std::string& str, const std::locale& loc = std::locale{})
{
	std::vector<wchar_t> buf(str.size());
	std::use_facet<std::ctype<wchar_t>>(loc).widen(str.data(), str.data() + str.size(), buf.data());

	return std::wstring(buf.data(), buf.size());
}

// convert wstring to string
inline std::string to_string(const std::wstring& str, const std::locale& loc = std::locale{})
{
	std::vector<char> buf(str.size());
	std::use_facet<std::ctype<wchar_t>>(loc).narrow(str.data(), str.data() + str.size(), '?', buf.data());

	return std::string(buf.data(), buf.size());
}

HMODULE GetCurrentModule()
{ // NB: XP+ solution!
	HMODULE hModule = NULL;
	GetModuleHandleEx(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		(LPCTSTR)GetCurrentModule,
		&hModule);

	return hModule;
}

bool wkekj;
HRESULT SomeCOMFunction(BSTR* bstr)
{
	*bstr = ::SysAllocString(L"Hello");
	return S_OK;
}
wchar_t const* const digitTables[] =
{
	L"0123456789",
	L"\u0660\u0661\u0662\u0663\u0664\u0665\u0666\u0667\u0668\u0669",
	// ...
};

int asNumeric(wchar_t wch)
{
	int result = -1;
	for (wchar_t const* const* p = std::begin(digitTables);
		p != std::end(digitTables) && result == -1;
		++p) {
		wchar_t const* q = std::find(*p, *p + 10, wch);
		if (q != *p + 10) {
			result = q - *p;
		}
		return result;
	}
}


VOID AddLine(ImGuiWindow& window, float width, float height, float a[3], float b[3], ImU32 color, float& minX, float& maxX, float& minY, float& maxY) {
	float ac[3] = { a[0], a[1], a[2] };
	float bc[3] = { b[0], b[1], b[2] };
	if (Utilities::WorldToScreen(width, height, ac) && Utilities::WorldToScreen(width, height, bc)) {
		window.DrawList->AddLine(ImVec2(ac[0], ac[1]), ImVec2(bc[0], bc[1]), color, 2.0f);

		minX = min(ac[0], minX);
		minX = min(bc[0], minX);

		maxX = max(ac[0], maxX);
		maxX = max(bc[0], maxX);

		minY = min(ac[1], minY);
		minY = min(bc[1], minY);

		maxY = max(ac[1], maxY);
		maxY = max(bc[1], maxY);
	}
}



ID3D11Device* pD11Device = nullptr;
ID3D11DeviceContext* pD11DeviceContext = nullptr;
ID3D11RenderTargetView* pD11RenderTargetView = nullptr;


HRESULT(*PresentOriginal)(IDXGISwapChain* pthis, UINT syncInterval, UINT flags) = nullptr;
using f_present = HRESULT(__stdcall*)(IDXGISwapChain* pthis, UINT sync_interval, UINT flags);
f_present o_present = nullptr;

HRESULT __stdcall hk_present(IDXGISwapChain* pthis, UINT sync_interval, UINT flags)
{
	static float width = 0;
	static float height = 0;
	static HWND hWnd = 0;

	if (!pD11Device || !pD11DeviceContext)
	{

		ImGui::CreateContext();

		if (SUCCEEDED(pthis->GetDevice(__uuidof(ID3D11Device), (void**)&pD11Device)))
		{
			pthis->GetDevice(__uuidof(pD11Device), (void**)&pD11Device);
			pD11Device->GetImmediateContext(&pD11DeviceContext);
		}

		ID3D11Texture2D* backBuffer = 0;
		pthis->GetBuffer(0, __uuidof(ID3D11Texture2D), (PVOID*)&backBuffer);
		D3D11_TEXTURE2D_DESC backBufferDesc = { 0 };
		backBuffer->GetDesc(&backBufferDesc);

		hWnd = FindWindow((L"UnrealWindow"), (L"Fortnite  "));
		if (!width)
		{
			oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Mouse)));

		}

		width = (float)backBufferDesc.Width;
		height = (float)backBufferDesc.Height;
		backBuffer->Release();

		ImGui::GetIO().Fonts->AddFontFromFileTTF(("C:\\Windows\\Fonts\\Fixedsys.ttf"), 12.0f);
		ImGui_ImplDX11_Init(hWnd, pD11Device, pD11DeviceContext);
		ImGui_ImplDX11_CreateDeviceObjects();

	}
	else
	{
		ID3D11Texture2D* renderTargetTexture = nullptr;
		if (!pD11RenderTargetView)
		{
			if (SUCCEEDED(pthis->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&renderTargetTexture))))
			{
				pD11Device->CreateRenderTargetView(renderTargetTexture, nullptr, &pD11RenderTargetView);
				renderTargetTexture->Release();
			}
		}
	}

	pD11DeviceContext->OMSetRenderTargets(1, &pD11RenderTargetView, nullptr);


	if (pD11RenderTargetView)
	{
		pD11RenderTargetView->Release();
		pD11RenderTargetView = nullptr;
	}


	auto& window = BeginScene();



	if (ShowMenu)
	{

		/*

		*/

		ImGuiStyle* style = &ImGui::GetStyle();

		style->Alpha = 1.0f;
		style->WindowPadding = ImVec2(16, 8);
		style->WindowMinSize = ImVec2(32, 32);
		style->WindowRounding = 0.0f;
		style->WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style->FramePadding = ImVec2(4, 3);
		style->FrameRounding = 0.0f;
		style->ItemSpacing = ImVec2(4, 3);
		style->ItemInnerSpacing = ImVec2(4, 4);
		style->TouchExtraPadding = ImVec2(0, 0);
		style->IndentSpacing = 21.0f;
		style->ColumnsMinSpacing = 3.0f;
		style->ScrollbarSize = 8.f;
		style->ScrollbarRounding = 0.0f;
		style->GrabMinSize = 1.0f;
		style->GrabRounding = 0.0f;
		style->ButtonTextAlign = ImVec2(0.5f, 0.5f);
		style->DisplayWindowPadding = ImVec2(22, 22);
		style->DisplaySafeAreaPadding = ImVec2(4, 4);
		style->AntiAliasedLines = true;
		style->CurveTessellationTol = 1.25f;



		ImColor mainColor = ImColor(int(54), int(54), int(54), 255);
		ImColor bodyColor = ImColor(int(24), int(24), int(24), 255);
		ImColor fontColor = ImColor(int(255), int(255), int(255), 255);

		ImVec4 mainColorHovered = ImVec4(mainColor.Value.x + 0.1f, mainColor.Value.y + 0.1f, mainColor.Value.z + 0.1f, mainColor.Value.w);
		ImVec4 mainColorActive = ImVec4(mainColor.Value.x + 0.2f, mainColor.Value.y + 0.2f, mainColor.Value.z + 0.2f, mainColor.Value.w);
		ImVec4 menubarColor = ImVec4(bodyColor.Value.x, bodyColor.Value.y, bodyColor.Value.z, bodyColor.Value.w - 0.8f);
		ImVec4 frameBgColor = ImVec4(bodyColor.Value.x, bodyColor.Value.y, bodyColor.Value.z, bodyColor.Value.w + .1f);
		ImVec4 tooltipBgColor = ImVec4(bodyColor.Value.x, bodyColor.Value.y, bodyColor.Value.z, bodyColor.Value.w + .05f);




		style->Colors[ImGuiCol_Text] = ImColor(254, 254, 254, 255);

		style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
		style->Colors[ImGuiCol_WindowBg] = ImColor(26, 26, 26, 255);
		style->Colors[ImGuiCol_ChildWindowBg] = ImColor(18, 18, 18, 255);
		style->Colors[ImGuiCol_PopupBg] = tooltipBgColor;
		style->Colors[ImGuiCol_Border] = ImColor(18, 18, 18, 0);
		style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
		style->Colors[ImGuiCol_FrameBg] = ImColor(14, 14, 14, 255);
		style->Colors[ImGuiCol_FrameBgHovered] = ImColor(15, 15, 15, 255);
		style->Colors[ImGuiCol_FrameBgActive] = ImColor(15, 15, 15, 255);
		style->Colors[ImGuiCol_TitleBg] = mainColor;
		style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
		style->Colors[ImGuiCol_TitleBgActive] = mainColor;
		style->Colors[ImGuiCol_MenuBarBg] = menubarColor;
		style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(frameBgColor.x + .05f, frameBgColor.y + .05f, frameBgColor.z + .05f, frameBgColor.w);
		style->Colors[ImGuiCol_ScrollbarGrab] = mainColor;
		style->Colors[ImGuiCol_ScrollbarGrabHovered] = mainColorHovered;
		style->Colors[ImGuiCol_ScrollbarGrabActive] = mainColorActive;
		style->Colors[ImGuiCol_CheckMark] = ImColor(240, 240, 240, 255);
		style->Colors[ImGuiCol_SliderGrab] = mainColorHovered;
		style->Colors[ImGuiCol_SliderGrabActive] = mainColorActive;
		style->Colors[ImGuiCol_Button] = ImColor(18, 18, 18, 255);
		style->Colors[ImGuiCol_ButtonHovered] = ImColor(128, 0, 128);
		style->Colors[ImGuiCol_ButtonActive] = ImColor(128, 0, 128);
		style->Colors[ImGuiCol_Header] = mainColor;
		style->Colors[ImGuiCol_HeaderHovered] = mainColorHovered;
		style->Colors[ImGuiCol_HeaderActive] = mainColorActive;

		style->Colors[ImGuiCol_Column] = mainColor;
		style->Colors[ImGuiCol_ColumnHovered] = mainColorHovered;
		style->Colors[ImGuiCol_ColumnActive] = mainColorActive;

		style->Colors[ImGuiCol_ResizeGrip] = mainColor;
		style->Colors[ImGuiCol_ResizeGripHovered] = mainColorHovered;
		style->Colors[ImGuiCol_ResizeGripActive] = mainColorActive;
		style->Colors[ImGuiCol_CloseButton] = mainColor;
		style->Colors[ImGuiCol_CloseButtonHovered] = mainColorHovered;
		style->Colors[ImGuiCol_CloseButtonActive] = mainColorActive;
		style->Colors[ImGuiCol_PlotLines] = mainColor;
		style->Colors[ImGuiCol_PlotLinesHovered] = mainColorHovered;
		style->Colors[ImGuiCol_PlotHistogram] = mainColor;
		style->Colors[ImGuiCol_PlotHistogramHovered] = mainColorHovered;
		style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
		style->Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);


#ifdef IMGUI_HAS_DOCK 
		style.TabBorderSize = is3D;
		style.TabRounding = 3;

		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

#endif


		ImGuiWindowFlags window_flags = 0;
		static bool no_titlebar = true;
		if (no_titlebar)        window_flags |= ImGuiWindowFlags_NoTitleBar;

		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.Fonts->AddFontFromFileTTF(("C:\\Windows\\Fonts\\Fixedsys.ttf"), 12);
		io.Fonts->AddFontFromFileTTF(("C:\\Windows\\Fonts\\Fixedsys.ttf"), 10);
		io.Fonts->AddFontFromFileTTF(("C:\\Windows\\Fonts\\Fixedsys.ttf"), 14);
		io.Fonts->AddFontFromFileTTF(("C:\\Windows\\Fonts\\Fixedsys.ttf"), 18);

		ImGui::SetNextWindowSize({ 650, 400 }, ImGuiCond_Always);
		ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

		std::string builddate = __DATE__;
		std::string buildtime = __TIME__;

		std::string kek = "BETA GlitchFN Build: " + builddate + "   " + __TIME__ + std::to_string(NULL);
		ImGui::Begin((kek.c_str()), reinterpret_cast<bool*>(true), ImGuiWindowFlags_NoCollapse); // start open
		float LineWitdh = 860;
		ImVec2 Loaction = ImGui::GetCursorScreenPos();
		float DynamicRainbow = 0.01;
		static float staticHue = 0;
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		staticHue -= DynamicRainbow;
		if (staticHue < -1.f) staticHue += 1.f;
		for (int i = 0; i < LineWitdh; i++)
		{
			float hue = staticHue + (1.f / (float)LineWitdh) * i;
			if (hue < 0.f) hue += 1.f;
			ImColor cRainbow = ImColor::HSV(hue, 1.f, 1.f);
			draw_list->AddRectFilled(ImVec2(Loaction.x + i, Loaction.y), ImVec2(Loaction.x + i + 1, Loaction.y + 4), cRainbow);
		}

		static int tabb = 0;
		ImGui::Text(" ");
		ImGui::Text(" ");
		{ImGui::SameLine();
		if (ImGui::Button(("Aimbot"), ImVec2(150.0f, 0.0f)))
		{
			tabb = 0;
		}
		ImGui::SameLine();

		if (ImGui::Button(("Visuals"), ImVec2(150.0f, 0.0f)))
		{
			tabb = 1;
		}
		ImGui::SameLine();
		if (ImGui::Button(("eXploits"), ImVec2(150.0f, 0.0f)))
		{
			tabb = 2;
		}
		{ImGui::SameLine();
		if (ImGui::Button(("Info"), ImVec2(150.0f, 0.0f)))
		{
			tabb = 3;
		}
		}
		}

		if (tabb == 0) {
			ImGui::Text("");
			ImGui::Text("Aim Type:");
			if (ImGui::BeginCombo("  ", current_aimmode, 1))
			{
				for (int n = 0; n < IM_ARRAYSIZE(aimmodes); n++)
				{
					bool is_selected = (current_aimmode == aimmodes[n]);
					if (ImGui::Selectable(aimmodes[n], is_selected))
						current_aimmode = aimmodes[n];

					if (is_selected)
						ImGui::SetItemDefaultFocus();

					if (current_aimmode == "  None") {
						options.Aimbot = false;
						options.SilentAimbot = false;
					}
					if (current_aimmode == "  Memory") {
						options.Aimbot = true;
						options.SilentAimbot = false;
					}
					if (current_aimmode == "  Silent") {
						options.Aimbot = false;
						options.SilentAimbot = true;
					}


				}
				ImGui::EndCombo();
			}
			ImGui::Checkbox(("WeakPoint"), &options.weakpointaim);
			ImGui::Checkbox(("Draw Croshair"), &options.ESP.crosshair);
			ImGui::Text("Aim Key");
			if (ImGui::BeginCombo("      ", current_aimkey, 2))
			{
				for (int n = 0; n < IM_ARRAYSIZE(aimkeys); n++)
				{
					bool is_selected = (current_aimkey == aimkeys[n]);
					if (ImGui::Selectable(aimkeys[n], is_selected))
						current_aimkey = aimkeys[n];

					if (is_selected)
						ImGui::SetItemDefaultFocus();

					if (current_aimkey == "  Left Click") {
						options.AimKey = "VK_LBUTTON";
					}
					if (current_aimkey == "  Right Click") {
						options.AimKey = "VK_RBUTTON";
					}
					if (current_aimkey == "  Middle Click") {
						options.AimKey = "VK_MBUTTON";
					}
					if (current_aimkey == "  CAPS") {
						options.AimKey = "VK_CAPITAL";
					}
					if (current_aimkey == "  Left Shift") {
						options.AimKey = "VK_LSHIFT";
					}
					if (current_aimkey == "  Alt") {
						options.AimKey = "VK_MENU";
					}
					if (current_aimkey == "  Q") {
						options.AimKey = "0x51";
					}
					if (current_aimkey == "  Z") {
						options.AimKey = "0x5A";
					}
					if (current_aimkey == "  X") {
						options.AimKey = "0x58";
					}
					if (current_aimkey == "  C") {
						options.AimKey = "0x43";
					}
					if (current_aimkey == "  `") {
						options.AimKey = "VK_OEM_3";

					}
				}
				ImGui::EndCombo();
			}
			ImGui::Text("Aim Part");
			if (ImGui::BeginCombo("     ", current_item, 3))
			{
				for (int n = 0; n < IM_ARRAYSIZE(items); n++)
				{
					bool is_selected = (current_item == items[n]);
					if (ImGui::Selectable(items[n], is_selected))
						current_item = items[n];

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}


			options.aimpart = current_item;
			ImGui::Text("Aim Fov");
			ImGui::SliderFloat(("         "), &options.AimbotFOV, 0.0f, 1000.0f, ("%.2f"));
			ImGui::Text("Aim Smoothing");
			ImGui::SliderFloat(("            "), &options.AimbotSlow, 0.0f, 25.0f, ("%.2f"));
		}

		else if (tabb == 1) {
			ImGui::Text("");
			ImGui::Checkbox(("Player ESP"), &options.ESP.Players);

			if (options.ESP.Players) {
				ImGui::Checkbox(("BoxESP"), &options.ESP.Box);
				if (options.ESP.Box) {

					ImGui::Text("Box Type");
					if (ImGui::BeginCombo("         ", current_boxtype, 4))
					{
						for (int n = 0; n < IM_ARRAYSIZE(boxtypes); n++)
						{
							bool is_selected = (current_boxtype == boxtypes[n]);
							if (ImGui::Selectable(boxtypes[n], is_selected))
								current_boxtype = boxtypes[n];

							if (is_selected)
								ImGui::SetItemDefaultFocus();

							if (current_boxtype == "  Cornered") {
								options.ESP.BoxType = "Cornered";
							}
							if (current_boxtype == "  Cornered Fill") {
								options.ESP.BoxType = "Cornered Fill";
							}
							if (current_boxtype == "  Box") {
								options.ESP.BoxType = "Box";
							}

						}
						ImGui::EndCombo();
					}

				}
				ImGui::Checkbox(("Snaplines"), &options.ESP.PlayerLines);
				if (options.ESP.PlayerLines) {

					ImGui::Text("Snapline Position");
					if (ImGui::BeginCombo("            ", current_snaplinelocation, 5))
					{
						for (int n = 0; n < IM_ARRAYSIZE(snaplinelocations); n++)
						{
							bool is_selected = (current_snaplinelocation == snaplinelocations[n]);
							if (ImGui::Selectable(snaplinelocations[n], is_selected))
								current_snaplinelocation = snaplinelocations[n];

							if (is_selected)
								ImGui::SetItemDefaultFocus();

							if (current_snaplinelocation == "  Top") {
								options.ESP.SnaplineLocation = "Top";
							}

							if (current_snaplinelocation == "  Centre") {
								options.ESP.SnaplineLocation = "Centre";
							}
							if (current_snaplinelocation == "  Bottom") {
								options.ESP.SnaplineLocation = "Bottom";
							}
						}
						ImGui::EndCombo();
					}

					ImGui::Checkbox(("PlayerNames"), &options.ESP.PlayerNames);
					ImGui::Spacing();

				}
				ImGui::Spacing();

				ImGui::PushItemWidth(150.0f);
			}
			ImGui::Checkbox(("Ammo ESP"), &options.ESP.Ammo);
			ImGui::Checkbox(("Weapon ESP"), &options.ESP.Weapons);
			ImGui::Checkbox(("Chest ESP"), &options.ESP.Containers);
			ImGui::Checkbox(("Boat ESP"), &options.ESP.Boat);
			ImGui::Checkbox(("Chopper ESP"), &options.ESP.Helicopter);
			ImGui::Text(" ");
			if (options.ESP.Weapons) {
				ImGui::SliderInt(("min weapon rariety"), &options.ESP.MinWeaponTier, 0, 6);
			}
		}



		else if (tabb == 2) {
			ImGui::Text("");
			ImGui::Checkbox(("SpeedHack"), &options.SpeedHack);
			ImGui::Checkbox(("Bullet TP"), &options.bullettp);
			ImGui::Checkbox(("Airstuck"), &options.Airstuck);
			ImGui::Checkbox(("SpinBot [CAPSLOCK]"), &options.Spinbot);
			ImGui::Checkbox(("Rocket TP"), &options.RocketTp);
			ImGui::Checkbox(("Rapid Fire (not Working)"), &options.Rapidfire);
			ImGui::Checkbox(("No Spread"), &options.NoSpreadAimbot);
			ImGui::Checkbox(("Instant Reload"), &options.InstantReload);
			ImGui::Checkbox(("Fov slider"), &options.fovslider);
			ImGui::SliderFloat(("FOV slider:"), &options.FOV, 60.0f, 160.0f, ("%.2f"));
			ImGui::Checkbox(("Freecam"), &options.Freecam);
			ImGui::SliderFloat(("Freecam Speed"), &options.CamSpeed, 0.0f, 35.0f, ("%.2f"));
			const char* Keybinds[] = { " Insert", " Capslock", " Tab", " Alt", " Left Shift", " Right Shift", " Left Ctrl", " Right Ctrl", " Minus (-)", " Plus (+)", " F1", " F2", " F3", " F4", " F5", " F6", " F7", " F8", " F9", " F10", " F11", " F12", " Left Mouse Button", " Right Mouse Button", " Middle Mouse Button", " None" };
			static int SpinbotKeybind = 1;
			static int Airstuck1Keybind = 9;
			static int Airstuck2Keybind = 8;
			ImGui::Combo("Spinbot", &SpinbotKeybind, Keybinds, IM_ARRAYSIZE(Keybinds));
			ImGui::Combo("Airstuck +", &Airstuck1Keybind, Keybinds, IM_ARRAYSIZE(Keybinds));
			ImGui::Combo("Airstuck -", &Airstuck2Keybind, Keybinds, IM_ARRAYSIZE(Keybinds));;
			int SpinbotBind = ImGui::FindKeybind(SpinbotKeybind);
			int Airstuck1Bind = ImGui::FindKeybind(Airstuck1Keybind);
			int Airstuck2Bind = ImGui::FindKeybind(Airstuck2Keybind);

		}
		ImGui::End();
	}


	auto success = FALSE;
	do {
		float closestDistance = FLT_MAX;
		PVOID closestPawn = NULL;

		auto world = *Offsets::uWorld;
		if (!world) break;

		auto gameInstance = ReadPointer(world, Offsets::Engine::World::OwningGameInstance);
		if (!gameInstance) break;

		auto localPlayers = ReadPointer(gameInstance, Offsets::Engine::GameInstance::LocalPlayers);
		if (!localPlayers) break;

		auto localPlayer = ReadPointer(localPlayers, 0);
		if (!localPlayer) break;

		auto localPlayerController = ReadPointer(localPlayer, Offsets::Engine::Player::PlayerController);
		if (!localPlayerController) break;

		auto localPlayerPawn = reinterpret_cast<UObject*>(ReadPointer(localPlayerController, Offsets::Engine::PlayerController::AcknowledgedPawn));
		if (!localPlayerPawn) break;

		auto localPlayerWeapon = ReadPointer(localPlayerPawn, Offsets::FortniteGame::FortPawn::CurrentWeapon);
		if (!localPlayerWeapon) break;

		auto localPlayerRoot = ReadPointer(localPlayerPawn, Offsets::Engine::Actor::RootComponent);
		if (!localPlayerRoot) break;

		auto localPlayerState = ReadPointer(localPlayerPawn, Offsets::Engine::Pawn::PlayerState);
		if (!localPlayerState) break;

		auto localPlayerLocation = reinterpret_cast<float*>(reinterpret_cast<PBYTE>(localPlayerRoot) + Offsets::Engine::SceneComponent::RelativeLocation);
		auto localPlayerTeamIndex = ReadDWORD(localPlayerState, Offsets::FortniteGame::FortPlayerStateAthena::TeamIndex);

		auto weaponName = Utilities::GetObjectFirstName((UObject*)localPlayerWeapon);
		auto isProjectileWeapon = wcsstr(weaponName.c_str(), L"Rifle_Sniper");

		Minipulation::LocalPlayerPawn = localPlayerPawn;
		Minipulation::LocalPlayerController = localPlayerController;
		std::vector<PVOID> boatPawns;

		std::vector<PVOID> playerPawns;
		for (auto li = 0UL; li < ReadDWORD(world, Offsets::Engine::World::Levels + sizeof(PVOID)); ++li) {
			auto levels = ReadPointer(world, Offsets::Engine::World::Levels);//Levels
			if (!levels) break;

			auto level = ReadPointer(levels, li * sizeof(PVOID));
			if (!level) continue;

			for (auto ai = 0UL; ai < ReadDWORD(level, Offsets::Engine::Level::AActors + sizeof(PVOID)); ++ai) {
				auto actors = ReadPointer(level, Offsets::Engine::Level::AActors);
				if (!actors) break;

				auto pawn = reinterpret_cast<UObject*>(ReadPointer(actors, ai * sizeof(PVOID)));
				if (!pawn || pawn == localPlayerPawn) continue;

				auto name = Utilities::GetObjectFirstName(pawn);
				if (wcsstr(name.c_str(), L"PlayerPawn_Athena_C") || wcsstr(name.c_str(), L"PlayerPawn_Athena_Phoebe_C") || wcsstr(name.c_str(), L"BP_MangPlayerPawn")) {
					playerPawns.push_back(pawn);
				}
				else if (wcsstr(name.c_str(), L"FortPickupAthena")) {
					auto item = ReadPointer(pawn, Offsets::FortniteGame::FortPickup::PrimaryPickupItemEntry + Offsets::FortniteGame::FortItemEntry::ItemDefinition);
					if (!item) continue;

					auto itemName = reinterpret_cast<FText*>(ReadPointer(item, Offsets::FortniteGame::FortItemDefinition::DisplayName));
					if (!itemName || !itemName->c_str()) continue;

					auto isAmmo = wcsstr(itemName->c_str(), L"Ammo: ");
					auto kek ReadBYTE(item, Offsets::FortniteGame::FortItemDefinition::Tier);
					if (kek <= options.ESP.MinWeaponTier) continue;
					std::wcout << L"\nBYTE:\\" << kek << L"\\";
					CHAR text[0xFF] = { 0 };
					wcstombs(text, itemName->c_str() + (isAmmo ? 6 : 0), sizeof(text));
					ImU32 common = ImGui::GetColorU32({ 123.0f, 123.0f, 123.0f, 1.0f }); //grey
					ImU32 noncommon = ImGui::GetColorU32({ 0.f, 1.f, 0.1f, 1.f }); // green
					ImU32 rare = ImGui::GetColorU32({ 0.2f, 0.3f, 1.f, 01.f }); // blue
					ImU32 epic = ImGui::GetColorU32({ 1.0f, 0.0f, 1.0f, 1.0f }); //purple
					ImU32 legendary = ImGui::GetColorU32({ 1.f, 0.5f, 0.05f, 1.0f }); //
					ImU32 mythic = ImGui::GetColorU32({ 1.f, 0.8f, 0.02f, 1.0f });

					if (kek == 0 && options.ESP.Ammo == true) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, ImGui::GetColorU32({ 0.75f, 0.75f, 0.75f, 1.0f }));
						std::cout << "\n 0";
					}
					else if (kek == 1) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, common);
						std::cout << "\n 1";
					}
					else if (kek == 2) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, noncommon);
						std::cout << "\n 2";
					}
					else if (kek == 3) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, rare);
						std::cout << "\n 3";
					}
					else if (kek == 4) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, epic);
						std::cout << "\n 4";
					}
					else if (kek == 5) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, legendary);
						std::cout << "\n 5";
					}
					else if (kek == 6) {
						AddMarker(window, width, height, localPlayerLocation, pawn, text, mythic);
						std::cout << "\n 6";
					}


				}

				else if (options.ESP.Containers && wcsstr(name.c_str(), L"Tiered_Chest") && !((ReadBYTE(pawn, Offsets::FortniteGame::BuildingContainer::bAlreadySearched) >> 7) & 1)) {
					AddMarker(window, width, height, localPlayerLocation, pawn, "Chest", ImGui::GetColorU32({ 1.0f, 0.84f, 0.0f, 1.0f }));
				}
				else if (options.ESP.Containers && wcsstr(name.c_str(), L"AthenaSupplyDrop_Llama")) {
					AddMarker(window, width, height, localPlayerLocation, pawn, "Llama", ImGui::GetColorU32({ 1.0f, 0.0f, 0.0f, 1.0f }));
				}
				else if (options.ESP.Ammo && wcsstr(name.c_str(), L"Tiered_Ammo") && !((ReadBYTE(pawn, Offsets::FortniteGame::BuildingContainer::bAlreadySearched) >> 7) & 1)) {
					AddMarker(window, width, height, localPlayerLocation, pawn, "Ammo Box", ImGui::GetColorU32({ 0.75f, 0.75f, 0.75f, 1.0f }));
				}
				else if (options.ESP.Helicopter && wcsstr(name.c_str(), L"HoagieVehicle_C") && !((ReadBYTE(pawn, Offsets::FortniteGame::BuildingContainer::bAlreadySearched) >> 7) & 1)) {
					AddMarker(window, width, height, localPlayerLocation, pawn, "Helicopter", ImGui::GetColorU32({ 1.0f, 0.84f, 0.0f, 1.0f }));
				}
				else if (options.ESP.Boat && wcsstr(name.c_str(), L"MeatballVehicle") && !((ReadBYTE(pawn, Offsets::FortniteGame::BuildingContainer::bAlreadySearched) >> 7) & 1)) {
					AddMarker(window, width, height, localPlayerLocation, pawn, "Boat", ImGui::GetColorU32({ 169.0f, 169.0f, 169.0f, 1.0f }));

				}
				else if (options.weakpointaim && wcsstr(name.c_str(), L"WeakSpot_C")) {
					AddMarker(window, width, height, localPlayerLocation, pawn, "WeakPoint", ImGui::GetColorU32({ 1.f, 0.f, 0.f, 1.0f }));
					FVector kekssda; //head
					if (!Minipulation::GetTargetHead(kekssda)) { //head
						goto As;
					}

					float angles[2] = { 0 };
					Utilities::CalcAngle(&Utilities::GetViewInfo().Location.X, &kekssda.X, angles); //head instead of neck.X

					FRotator args = { 0 };
					args.Pitch = angles[0];
					args.Yaw = angles[1];
					Minipulation::ProcessEvent(Minipulation::LocalPlayerController, Offsets::Engine::Controller::SetControlRotation, &args, 0);

				}
			As:
				if (wkekj == true)
				{
					std::string dam(name.begin(), name.end());
					AddMarker(window, width, height, localPlayerLocation, pawn, dam.c_str(), ImGui::GetColorU32({ 1.f, 0.f, 0.f, 1.0f }));
				}
			}
		}

		for (auto boatPawn : boatPawns)
		{
			FVector viewpoint = { 0 };
			if (Minipulation::LocalPlayerController == nullptr)
				continue;

			if (Utilities::LineOfSightTo(Minipulation::LocalPlayerController, boatPawn, &viewpoint))
			{
				Minipulation::TargetBoat = boatPawn;
			}
		}


		for (auto pawn : playerPawns)
		{
			auto state = ReadPointer(pawn, Offsets::Engine::Pawn::PlayerState);
			if (!state) continue;

			auto mesh = ReadPointer(pawn, Offsets::Engine::Character::Mesh);
			if (!mesh) continue;

			auto bones = ReadPointer(mesh, Offsets::Engine::StaticMeshComponent::StaticMesh);
			if (!bones) bones = ReadPointer(mesh, Offsets::Engine::StaticMeshComponent::StaticMesh + 0x10);
			if (!bones) continue;

			float compMatrix[4][4] = { 0 };
			Utilities::ToMatrixWithScale(reinterpret_cast<float*>(reinterpret_cast<PBYTE>(mesh) + 0x1C0), compMatrix);

			// Top
			float head[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 66, head);

			float neck[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 65, neck);

			float chest[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 36, chest);

			float pelvis[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 2, pelvis);

			// Arms
			float leftShoulder[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 9, leftShoulder);

			float rightShoulder[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 62, rightShoulder);

			float leftElbow[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 10, leftElbow);

			float rightElbow[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 38, rightElbow);

			float leftHand[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 11, leftHand);

			float rightHand[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 39, rightHand);

			// Legs
			float leftLeg[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 67, leftLeg);

			float rightLeg[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 74, rightLeg);

			float leftThigh[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 73, leftThigh);

			float rightThigh[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 80, rightThigh);

			float leftFoot[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 68, leftFoot);

			float rightFoot[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 75, rightFoot);

			float leftFeet[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 71, leftFeet);

			float rightFeet[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 78, rightFeet);

			float leftFeetFinger[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 72, leftFeetFinger);

			float rightFeetFinger[3] = { 0 };
			Utilities::GetBoneLocation(compMatrix, bones, 79, rightFeetFinger);

			auto color = ImGui::GetColorU32({ red });
			auto dickhead = ImGui::GetColorU32({ white });
			FVector viewPoint = { 0 };
			bool lineofsightk2 = false;
			if (ReadDWORD(state, Offsets::FortniteGame::FortPlayerStateAthena::TeamIndex) == localPlayerTeamIndex) {
				color = ImGui::GetColorU32({ 0.0f, 1.0f, 0.0f, 1.0f });
			}
			else if ((ReadBYTE(pawn, Offsets::FortniteGame::FortPawn::bIsDBNO) & 1) && (isProjectileWeapon || Utilities::LineOfSightTo(localPlayerController, pawn, &viewPoint))) {
				lineofsightk2 = true;
				color = ImGui::GetColorU32({ green });
				if (options.AutoAimbot) {
					auto dx = head[0] - localPlayerLocation[0];
					auto dy = head[1] - localPlayerLocation[1];
					auto dz = head[2] - localPlayerLocation[2];
					auto dist = dx * dx + dy * dy + dz * dz;
					if (dist < closestDistance) {
						closestDistance = dist;
						closestPawn = pawn;


					}
				}
				else
				{
					auto w2s = *reinterpret_cast<FVector*>(head);
					if (Utilities::WorldToScreen(width, height, &w2s.X)) {
						auto dx = w2s.X - (width / 2);
						auto dy = w2s.Y - (height / 2);

						auto dist = Utilities::SpoofCall(sqrtf, dx * dx + dy * dy);
						if (dist < options.AimbotFOV && dist < closestDistance) {
							closestDistance = dist;
							closestPawn = pawn;


						}
					}
				}
			}

			if (ImGui::Begin("my radar", &options.ESP.Radar, ImVec2(200, 200), 0.9f, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar))
			{
				ImDrawList* Draw = ImGui::GetOverlayDrawList();
				ImVec2 DrawPos = ImGui::GetCursorScreenPos();
				ImVec2 DrawSize = ImGui::GetContentRegionAvail();
				Draw->AddLine(
					ImVec2(DrawPos.x + DrawSize.x / 2.f, DrawPos.y),
					ImVec2(DrawPos.x + DrawSize.x / 2.f, DrawPos.y + DrawSize.y),
					ImColor(1.f, 1.f, 1.f));

				Draw->AddLine(
					ImVec2(DrawPos.x, DrawPos.y + DrawSize.y / 2.f),
					ImVec2(DrawPos.x + DrawSize.x, DrawPos.y + DrawSize.y / 2.f),
					ImColor(1.f, 1.f, 1.f));

				FRotator vAngle = Utilities::GetViewInfo().Rotation;
				auto fYaw = vAngle.Yaw * PI / 180.0f;
				float dx = localPlayerLocation[0];
				float dy = localPlayerLocation[1];

				//float fYaw = float(vAngle.Yaw * (M_PI / 180.0));

				float fsin_yaw = sinf(fYaw);
				float fminus_cos_yaw = -cosf(fYaw);

				float x = dy * fminus_cos_yaw + dx * fsin_yaw;
				float y = dx * fminus_cos_yaw - dy * fsin_yaw;
			}
			ImGui::End();



			//if (!options.ESP.Players) continue;


			if (options.ESP.crosshair) {
				window.DrawList->AddLine(ImVec2(width / 2 - 15, height / 2), ImVec2(width / 2 + 15, height / 2), ImGui::GetColorU32(white), 2);
				window.DrawList->AddLine(ImVec2(width / 2, height / 2 - 15), ImVec2(width / 2, height / 2 + 15), ImGui::GetColorU32(white), 2);
			}

			if (options.ESP.PlayerLines) {

				if (options.ESP.SnaplineLocation == "Top") {

					auto end = *reinterpret_cast<FVector*>(head);
					if (Utilities::WorldToScreen(width, height, &end.X)) {
						if (lineofsightk2)
						{
							window.DrawList->AddLine(ImVec2(width / 2, height - 1080), ImVec2(end.X, end.Y), ImGui::GetColorU32({ green }));
						}
						else
						{
							//	window.DrawList->AddLine(ImVec2(width / 2, height - 1080), ImVec2(end.X, end.Y), color);
							window.DrawList->AddLine(ImVec2(width / 2, height - 1080), ImVec2(end.X, end.Y), ImGui::GetColorU32({ red }));
						}
					}

				}


				if (options.ESP.SnaplineLocation == "Centre") {

					auto end = *reinterpret_cast<FVector*>(head);
					if (Utilities::WorldToScreen(width, height, &end.X)) {
						if (lineofsightk2)
						{
							window.DrawList->AddLine(ImVec2(width / 2, height - 540), ImVec2(end.X, end.Y), ImGui::GetColorU32({ green }));
						}
						else
						{
							//	window.DrawList->AddLine(ImVec2(width / 2, height - 1080), ImVec2(end.X, end.Y), color);
							window.DrawList->AddLine(ImVec2(width / 2, height - 540), ImVec2(end.X, end.Y), ImGui::GetColorU32({ red }));
						}
					}

				}

				if (options.ESP.SnaplineLocation == "Bottom") {

					auto end = *reinterpret_cast<FVector*>(head);
					if (Utilities::WorldToScreen(width, height, &end.X)) {
						if (lineofsightk2)
						{
							window.DrawList->AddLine(ImVec2(width / 2, height), ImVec2(end.X, end.Y), ImGui::GetColorU32({ green }));
						}
						else
						{
							//	window.DrawList->AddLine(ImVec2(width / 2, height - 1080), ImVec2(end.X, end.Y), color);
							window.DrawList->AddLine(ImVec2(width / 2, height), ImVec2(end.X, end.Y), ImGui::GetColorU32({ red }));
						}
					}

				}

			}



			float minX = FLT_MAX;
			float maxX = -FLT_MAX;
			float minY = FLT_MAX;
			float maxY = -FLT_MAX;

			AddLine(window, width, height, head, neck, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, neck, pelvis, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, chest, leftShoulder, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, chest, rightShoulder, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, leftShoulder, leftElbow, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, rightShoulder, rightElbow, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, leftElbow, leftHand, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, rightElbow, rightHand, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, pelvis, leftLeg, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, pelvis, rightLeg, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, leftLeg, leftThigh, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, rightLeg, rightThigh, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, leftThigh, leftFoot, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, rightThigh, rightFoot, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, leftFoot, leftFeet, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, rightFoot, rightFeet, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, leftFeet, leftFeetFinger, color, minX, maxX, minY, maxY);
			AddLine(window, width, height, rightFeet, rightFeetFinger, color, minX, maxX, minY, maxY);


			auto root = Utilities::GetPawnRootLocation(pawn);
			float dx;
			float dy;
			float dz;
			float dist;
			if (root) {
				auto pos = *root;
				dx = localPlayerLocation[0] - pos.X;
				dy = localPlayerLocation[1] - pos.Y;
				dz = localPlayerLocation[2] - pos.Z;

				if (Utilities::WorldToScreen(width, height, &pos.X)) {
					dist = Utilities::SpoofCall(sqrtf, dx * dx + dy * dy + dz * dz) / 1500.0f;
				}
			}

			if (dist >= 100)
				dist = 75;

			if (minX < width && maxX > 0 && minY < height && maxY > 0) {
				auto topLeft = ImVec2(minX - 3.0f, minY - 3.0f);
				auto bottomRight = ImVec2(maxX + 3.0f, maxY + 3.0f);
				float lineW = (width / 5);
				float lineH = (height / 6);
				float lineT = 1;

				auto w2sa = *reinterpret_cast<FVector*>(head);
				Utilities::WorldToScreen(width, height, &w2sa.X);
				Utilities::WorldToScreen(width, height, &w2sa.Y);
				auto X = w2sa.X;
				auto Y = w2sa.Y;


				auto bottomRightLEFT = ImVec2(maxX - options.CornerSize + dist, maxY + 2.5f);
				auto bottomRightUP = ImVec2(maxX + 3.0f, maxY - options.CornerSize + dist);
				auto topRight = ImVec2(maxX + 3.0f, minY - 3.0f);
				auto topRightLEFT = ImVec2(maxX - options.CornerSize + dist, minY - 3.0f);
				auto topRightDOWN = ImVec2(maxX + 3.0f, minY + options.CornerSize - dist);

				auto bottomLeft = ImVec2(minX - 3.0f, maxY + 3.f);
				auto bottomLeftRIGHT = ImVec2(minX + options.CornerSize - dist, maxY + 3.f);
				auto bottomLeftUP = ImVec2(minX - 3.0f, maxY - options.CornerSize + dist);
				auto topLeftRIGHT = ImVec2(minX + options.CornerSize - dist, minY - 3.0f);
				auto topLeftDOWN = ImVec2(minX - 3.0f, minY + options.CornerSize - dist);

				if (options.ESP.Box && options.ESP.BoxType == "Cornered") {

					ImU32 kek = ImGui::GetColorU32({ ImGui::GetColorU32({ 1.f, 0.f, 0.f, 1.0f }) });
					window.DrawList->AddLine(topLeft, topLeftRIGHT, ImGui::GetColorU32({ white }), 1.00f);
					window.DrawList->AddLine(topLeft, topLeftDOWN, ImGui::GetColorU32({ white }), 1.00f);

					window.DrawList->AddLine(bottomRight, bottomRightLEFT, ImGui::GetColorU32({ white }), 1.5f);
					window.DrawList->AddLine(bottomRight, bottomRightUP, ImGui::GetColorU32({ white }), 1.5f);

					window.DrawList->AddLine(topRight, topRightLEFT, ImGui::GetColorU32({ white }), 1.5f);
					window.DrawList->AddLine(topRight, topRightDOWN, ImGui::GetColorU32({ white }), 1.5f);

					window.DrawList->AddLine(bottomLeft, bottomLeftRIGHT, ImGui::GetColorU32({ white }), 1.5f);
					window.DrawList->AddLine(bottomLeft, bottomLeftUP, ImGui::GetColorU32({ white }), 1.5f);

				}
				if (options.ESP.Box && options.ESP.BoxType == "BoxA") {

					if (minX < width && maxX > 0 && minY < height && maxY > 0) {
						auto topLeft = ImVec2(minX - 4.0f, minY - 4.0f);
						auto bottomRight = ImVec2(maxX + 4.0f, maxY + 4.0f);



						if (options.ESP.box)
						{
							window.DrawList->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32({ 0.0f, 0.0f, 0.0f, 0.20f }));
							window.DrawList->AddRect(topLeft, bottomRight, ImGui::GetColorU32({ 0.0f, 0.50f, 0.90f, 1.0f }), 0.5, 15, 1.5f);

						}

					}

				}

				if (options.ESP.Box && options.ESP.BoxType == "Cornered Fill") {


					window.DrawList->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32({ 0.0f, 0.0f, 0.0f, 0.50f }));
					ImU32 kek = ImGui::GetColorU32({ ImGui::GetColorU32({ 1.f, 0.f, 0.f, 1.0f }) });
					window.DrawList->AddLine(topLeft, topLeftRIGHT, ImGui::GetColorU32({ white }), 1.00f);
					window.DrawList->AddLine(topLeft, topLeftDOWN, ImGui::GetColorU32({ white }), 1.00f);

					window.DrawList->AddLine(bottomRight, bottomRightLEFT, ImGui::GetColorU32({ white }), 1.5f);
					window.DrawList->AddLine(bottomRight, bottomRightUP, ImGui::GetColorU32({ white }), 1.5f);

					window.DrawList->AddLine(topRight, topRightLEFT, ImGui::GetColorU32({ white }), 1.5f);
					window.DrawList->AddLine(topRight, topRightDOWN, ImGui::GetColorU32({ white }), 1.5f);

					window.DrawList->AddLine(bottomLeft, bottomLeftRIGHT, ImGui::GetColorU32({ white }), 1.5f);
					window.DrawList->AddLine(bottomLeft, bottomLeftUP, ImGui::GetColorU32({ white }), 1.5f);

				}

				if (options.ESP.Box && options.ESP.BoxType == "Box") {

					if (minX < width && maxX > 0 && minY < height && maxY > 0) {
						auto Spikey1 = ImVec2(maxX + 4.0f, maxY + 4.0f);
						auto Spikey2 = ImVec2(minX - 4.0f, minY - 4.0f);;



						if (options.ESP.box)
						{
							window.DrawList->AddRect(Spikey1, Spikey2, ImGui::GetColorU32({ white }), 0.5, 15, 1.5f);
						}

					}
				}






				if (options.ESP.PlayerNames) {
					FString playerName;
					Minipulation::ProcessEvent(state, Offsets::Engine::PlayerState::GetPlayerName, &playerName, 0);
					if (playerName.c_str()) {
						CHAR copy[0xFF] = { 0 };
						auto w2s = *reinterpret_cast<FVector*>(head);
						float dist;
						if (Utilities::WorldToScreen(width, height, &w2s.X)) {
							auto dx = w2s.X;
							auto dy = w2s.Y;
							auto dz = w2s.Z;
							dist = Utilities::SpoofCall(sqrtf, dx * dx + dy * dy + dz * dz) / 100.0f;
						}
						CHAR lel[0xFF] = { 0 };
						wcstombs(lel, playerName.c_str(), sizeof(lel));
						Utilities::FreeInternal(playerName.c_str());
						snprintf(copy, sizeof(copy), ("%s [%dm]"), lel, static_cast<INT>(dist));
						auto centerTop = ImVec2((topLeft.x + bottomRight.x) / 2.0f, topLeft.y);
						auto size = ImGui::GetFont()->CalcTextSizeA(window.DrawList->_Data->FontSize, FLT_MAX, 0, copy);
						//	window.DrawList->AddRectFilled(ImVec2(centerTop.x - size.x / 2.0f, centerTop.y - size.y + 3.0f), ImVec2(centerTop.x + size.x / 2.0f, centerTop.y), ImGui::GetColorU32({ 0.0f, 0.0f, 0.0f, 0.4f }));
						ImVec2 kek = ImVec2(centerTop.x - size.x / 2.0f + 10, centerTop.y - size.y);
						//window.DrawList->AddRectFilled(kek, ImVec2(centerTop.y - size.y), ImGui::GetColorU32({ 0.0f, 0.0f, 0.0f, 0.20f }));
						std::string jsj = copy;
						if (jsj.find("Shawn") != std::string::npos) {
							window.DrawList->AddText(ImVec2(centerTop.x - size.x / 2.0f + 10, centerTop.y - size.y), ImGui::GetColorU32({ 1.0f, 0.0f, 1.0f, 1.0f }), copy);
						}
						else
						{
							window.DrawList->AddText(ImVec2(centerTop.x - size.x / 2.0f + 10, centerTop.y - size.y), color, copy);
						}
					}
				}
			}
		}



		if (options.AimKey == "VK_RBUTTON") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_RBUTTON) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_RBUTTON) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "VK_LBUTTON") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_LBUTTON) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_LBUTTON) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "VK_MBUTTON") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_MBUTTON) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_MBUTTON) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "VK_CAPITAL") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_CAPITAL) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_CAPITAL) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "VK_LSHIFT") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_LSHIFT) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_LSHIFT) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "VK_MENU") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_MENU) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_MENU) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "0x51") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x51) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x51) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "0x5A") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x5A) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x5A) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "0x58") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x58) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x58) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "0x43") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x43) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, 0x43) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (options.AimKey == "VK_OEM_3") {

			if (options.Aimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_OEM_3) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else if (options.SilentAimbot && closestPawn && Utilities::SpoofCall(GetAsyncKeyState, VK_OEM_3) < 0 && Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				Minipulation::TargetPawn = closestPawn;
				Minipulation::NoSpread = options.NoSpreadAimbot;
				//	printf("\nworked?");
			}
			else {
				Minipulation::TargetPawn = nullptr;
				Minipulation::NoSpread = options.NoSpreadAimbot;
			}

		}

		if (!options.AutoAimbot && options.ESP.AimbotFOV) {
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.20f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 1, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.20f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 2, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.18f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 3, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.18f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 4, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.16f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 5, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.16f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 6, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.14f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 7, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.14f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 8, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.12f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 9, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.12f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 10, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.12f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 11, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.10f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 12, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.10f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 13, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.10f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 14, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.08f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 15, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.08f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 16, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.08f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 17, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.06f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 18, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.06f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 19, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.06f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 20, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.04f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 21, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.04f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 22, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.04f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 23, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.02f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 24, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.02f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 25, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.02f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 26, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.01f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 27, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.01f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 28, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.005f }), 105);
			window.DrawList->AddCircle(ImVec2(width / 2, height / 2), options.AimbotFOV + 29, ImGui::GetColorU32({ 255.0,255.0,255.0, 0.005f }), 105);

		}
		success = TRUE;
	} while (FALSE);

	if (!success) {
		Minipulation::LocalPlayerController = Minipulation::LocalPlayerPawn = Minipulation::TargetPawn = nullptr;
	}

	EndScene(window);

	return o_present(pthis, sync_interval, flags);
}

namespace Minipulation {
	bool NoSpread = false;

	PVOID LocalPlayerPawn = nullptr;
	PVOID LocalPlayerController = nullptr;
	PVOID TargetPawn = nullptr;
	BYTE FreeCamDirection[6] = { 0 };
	FVector FreeCamPosition = { 0 };
	INT(*GetViewPoint)(PVOID, FMinimalViewInfo*, BYTE) = nullptr;
	PVOID(*ProcessEvent)(PVOID, PVOID, PVOID, PVOID) = nullptr;
	PVOID(*CalculateSpread)(PVOID, float*, float*) = nullptr;
	float* (*CalculateShot)(PVOID, PVOID, PVOID) = nullptr;
	VOID(*ReloadOriginal)(PVOID, PVOID) = nullptr;
	PVOID(*GetWeaponStats)(PVOID) = nullptr;
	bool IsAirstuck = true;
	int Current = 1;
	auto CurrentLocation = Minipulation::FreeCamPosition;
	auto CurrentYaw = 0;
	auto CurrentPitch = 0;
	auto CurrenRoll = 0;
	auto OriginalAitstuck = 0;
	PVOID calculateSpreadCaller = nullptr;
	float originalReloadTime = 0.0f;

	BOOLEAN GetTargetHead(FVector& out) {
		if (!Minipulation::TargetPawn) {
			return FALSE;
		}

		auto mesh = ReadPointer(Minipulation::TargetPawn, 0x278);
		if (!mesh) {
			return FALSE;
		}

		auto bones = ReadPointer(mesh, 0x420);
		if (!bones) bones = ReadPointer(mesh, 0x420 + 0x10);
		if (!bones) {
			return FALSE;
		}

		float compMatrix[4][4] = { 0 };
		Utilities::ToMatrixWithScale(reinterpret_cast<float*>(reinterpret_cast<PBYTE>(mesh) + 0x1C0), compMatrix);

		Utilities::GetBoneLocation(compMatrix, bones, 66, &out.X);
		return TRUE;
	}

	BOOLEAN GetTargetChest(FVector& out) {
		if (!Minipulation::TargetPawn) {
			return FALSE;
		}

		auto mesh = ReadPointer(Minipulation::TargetPawn, 0x278);
		if (!mesh) {
			return FALSE;
		}

		auto bones = ReadPointer(mesh, 0x420);
		if (!bones) bones = ReadPointer(mesh, 0x420 + 0x10);
		if (!bones) {
			return FALSE;
		}

		float compMatrix[4][4] = { 0 };
		Utilities::ToMatrixWithScale(reinterpret_cast<float*>(reinterpret_cast<PBYTE>(mesh) + 0x1C0), compMatrix);

		Utilities::GetBoneLocation(compMatrix, bones, 7, &out.X);
		return TRUE;
	}

	BOOLEAN GetTargetLeg(FVector& out) {
		if (!Minipulation::TargetPawn) {
			return FALSE;
		}

		auto mesh = ReadPointer(Minipulation::TargetPawn, 0x278);
		if (!mesh) {
			return FALSE;
		}

		auto bones = ReadPointer(mesh, 0x420);
		if (!bones) bones = ReadPointer(mesh, 0x420 + 0x10);
		if (!bones) {
			return FALSE;
		}

		float compMatrix[4][4] = { 0 };
		Utilities::ToMatrixWithScale(reinterpret_cast<float*>(reinterpret_cast<PBYTE>(mesh) + 0x1C0), compMatrix);

		Utilities::GetBoneLocation(compMatrix, bones, 73, &out.X);
		return TRUE;
	}


	BOOLEAN GetTargetDick(FVector& out) {
		if (!Minipulation::TargetPawn) {
			return FALSE;
		}

		auto mesh = ReadPointer(Minipulation::TargetPawn, 0x278);
		if (!mesh) {
			return FALSE;
		}

		auto bones = ReadPointer(mesh, 0x420);
		if (!bones) bones = ReadPointer(mesh, 0x420 + 0x10);
		if (!bones) {
			return FALSE;
		}

		float compMatrix[4][4] = { 0 };
		Utilities::ToMatrixWithScale(reinterpret_cast<float*>(reinterpret_cast<PBYTE>(mesh) + 0x1C0), compMatrix);

		Utilities::GetBoneLocation(compMatrix, bones, 2, &out.X);
		return TRUE;
	}


	PVOID ProcessEventHook(UObject* object, UObject* func, PVOID params, PVOID result) {

		if (object && func) {
			auto objectName = Utilities::GetObjectFirstName(object);
			auto funcName = Utilities::GetObjectFirstName(func);
			do {
				if (Minipulation::TargetPawn && Minipulation::LocalPlayerController) {
					if (wcsstr(objectName.c_str(), L"B_Prj_Bullet_Sniper") && funcName == L"OnRep_FireStart" && options.bullettp == true) {
						FVector head = { 0 };
						if (!GetTargetHead(head)) {
							break;
						}

						*reinterpret_cast<FVector*>(reinterpret_cast<PBYTE>(object) + Offsets::FortniteGame::FortProjectileAthena::FireStartLoc) = head;

						auto root = reinterpret_cast<PBYTE>(ReadPointer(object, Offsets::Engine::Actor::RootComponent));
						*reinterpret_cast<FVector*>(root + Offsets::Engine::SceneComponent::RelativeLocation) = head;
						memset(root + Offsets::Engine::SceneComponent::ComponentVelocity, 0, sizeof(FVector));
					}
					if (wcsstr(objectName.c_str(), L"SM_Ostrich_Rocket_01") && funcName == L"OnRep_FireStart" && options.RocketTp) {
						FVector head = { 0 };
						if (!GetTargetHead(head)) {
							break;
						}
						*reinterpret_cast<FVector*>(reinterpret_cast<PBYTE>(object) + Offsets::FortniteGame::FortProjectileAthena::FireStartLoc) = head;
						auto root = reinterpret_cast<PBYTE>(ReadPointer(object, Offsets::Engine::Actor::RootComponent));
						*reinterpret_cast<FVector*>(root + Offsets::Engine::SceneComponent::RelativeLocation) = head;
						memset(root + Offsets::Engine::SceneComponent::ComponentVelocity, 0, sizeof(FVector));
					}
					else if (!options.SilentAimbot && wcsstr(funcName.c_str(), L"Tick")) {
						FVector neck;
						if (options.aimpart == "  Head")
						{
							if (!GetTargetHead(neck)) {
								break;
							}
						}
						else if (options.aimpart == "  Chest")
						{
							if (!GetTargetChest(neck)) {
								break;
							}
						}
						else if (options.aimpart == "  Leg")
						{
							if (!GetTargetLeg(neck)) {
								break;
							}
						}
						else if (options.aimpart == "  Pelvis")
						{
							if (!GetTargetDick(neck)) {
								break;
							}
						}
						float angles[2] = { 0 };
						Utilities::CalcAngle(&Utilities::GetViewInfo().Location.X, &neck.X, angles);

						if (options.AimbotSlow <= 0.0f) {
							FRotator args = { 0 };
							args.Pitch = angles[0];
							args.Yaw = angles[1];
							ProcessEvent(Minipulation::LocalPlayerController, Offsets::Engine::Controller::SetControlRotation, &args, 0);
						}
						else {
							auto scale = options.AimbotSlow + 1.0f;
							auto currentRotation = Utilities::GetViewInfo().Rotation;

							FRotator args = { 0 };
							args.Pitch = (angles[0] - currentRotation.Pitch) / scale + currentRotation.Pitch;
							args.Yaw = (angles[1] - currentRotation.Yaw) / scale + currentRotation.Yaw;
							ProcessEvent(Minipulation::LocalPlayerController, Offsets::Engine::Controller::SetControlRotation, &args, 0);
						}
					}
				}

			} while (FALSE);
		}

		return ProcessEvent(object, func, params, result);
	}

	PVOID CalculateSpreadHook(PVOID arg0, float* arg1, float* arg2) {
		if (originalReloadTime != 0.0f) {
			auto localPlayerWeapon = ReadPointer(Minipulation::LocalPlayerPawn, Offsets::FortniteGame::FortPawn::CurrentWeapon);
			if (localPlayerWeapon) {
				auto stats = GetWeaponStats(localPlayerWeapon);
				if (stats) {
					*reinterpret_cast<float*>(reinterpret_cast<PBYTE>(stats) + Offsets::FortniteGame::FortBaseWeaponStats::ReloadTime) = originalReloadTime;
					originalReloadTime = 0.0f;
				}
			}
		}


		if (options.NoSpreadAimbot && Minipulation::NoSpread && _ReturnAddress() == calculateSpreadCaller) {
			return 0;
		}

		return CalculateSpread(arg0, arg1, arg2);
	}

	float* CalculateShotHook(PVOID arg0, PVOID arg1, PVOID arg2) {
		auto ret = CalculateShot(arg0, arg1, arg2);
		if (ret && options.SilentAimbot && Minipulation::TargetPawn && Minipulation::LocalPlayerPawn) {
			auto mesh = ReadPointer(Minipulation::TargetPawn, Offsets::Engine::Character::Mesh);
			if (!mesh) return ret;

			auto bones = ReadPointer(mesh, Offsets::Engine::StaticMeshComponent::StaticMesh);
			if (!bones) bones = ReadPointer(mesh, Offsets::Engine::StaticMeshComponent::StaticMesh + 0x10);
			if (!bones) return ret;

			float compMatrix[4][4] = { 0 };
			Utilities::ToMatrixWithScale(reinterpret_cast<float*>(reinterpret_cast<PBYTE>(mesh) + Offsets::Engine::StaticMeshComponent::ComponentToWorld), compMatrix);

			FVector head = { 0 };
			if (options.aimpart == "  Head")
			{
				Utilities::GetBoneLocation(compMatrix, bones, 66, &head.X);
			}
			else if (options.aimpart == "  Chest")
			{
				Utilities::GetBoneLocation(compMatrix, bones, 7, &head.X);
			}
			else if (options.aimpart == "  Leg")
			{
				Utilities::GetBoneLocation(compMatrix, bones, 73, &head.X);
			}
			else if (options.aimpart == "  Pelvis")
			{
				Utilities::GetBoneLocation(compMatrix, bones, 2, &head.X);
			}
			std::cout << "\n" << options.aimpart;
			auto rootPtr = Utilities::GetPawnRootLocation(Minipulation::LocalPlayerPawn);
			if (!rootPtr) return ret;
			auto root = *rootPtr;

			auto dx = head.X - root.X;
			auto dy = head.Y - root.Y;
			auto dz = head.Z - root.Z;
			if (dx * dx + dy * dy + dz * dz < 125000.0f) {
				ret[4] = head.X;
				ret[5] = head.Y;
				ret[6] = head.Z;
			}
			else {
				head.Z -= 16.0f;
				root.Z += 45.0f;

				auto y = atan2f(head.Y - root.Y, head.X - root.X);

				root.X += cosf(y + 1.5708f) * 32.0f;
				root.Y += sinf(y + 1.5708f) * 32.0f;

				auto length = Utilities::SpoofCall(sqrtf, powf(head.X - root.X, 2) + powf(head.Y - root.Y, 2));
				auto x = -atan2f(head.Z - root.Z, length);
				y = atan2f(head.Y - root.Y, head.X - root.X);

				x /= 2.0f;
				y /= 2.0f;

				ret[0] = -(sinf(x) * sinf(y));
				ret[1] = sinf(x) * cosf(y);
				ret[2] = cosf(x) * sinf(y);
				ret[3] = cosf(x) * cosf(y);
			}
		}

		return ret;
	}

	INT GetViewPointHook(PVOID player, FMinimalViewInfo* viewInfo, BYTE stereoPass)
	{
		static FVector freeCamVelocity = { 0 };
		const float flySpeed = 0.10f;
		const float upperFOV = 50.534008f;
		const float lowerFOV = 40.0f;

		auto ret = GetViewPoint(player, viewInfo, stereoPass);

		auto fov = viewInfo->FOV;
		auto desired = (((180.0f - upperFOV) / (180.0f - 80.0f)) * (options.FOV - 80.0f)) + upperFOV;

		if (fov > upperFOV) {
			fov = desired;
		}
		else if (fov > lowerFOV) {
			fov = (((fov - lowerFOV) / (upperFOV - lowerFOV)) * (desired - lowerFOV)) + lowerFOV;
		}

		if (options.fovslider == true)
		{
			viewInfo->FOV = fov;
		}


		if (options.Freecam)
		{
			FVector v =
			{
				static_cast<float>(Minipulation::FreeCamDirection[0] - Minipulation::FreeCamDirection[1]),
				static_cast<float>(Minipulation::FreeCamDirection[2] - Minipulation::FreeCamDirection[3]),
				static_cast<float>(Minipulation::FreeCamDirection[4] - Minipulation::FreeCamDirection[5]),
			};
			auto m = Utilities::SpoofCall(sqrtf, v.X * v.X + v.Y * v.Y + v.Z * v.Z);
			if (m != 0)
			{
				v.X /= m;
				v.Y /= m;
				v.Z /= m;
			}

			auto r = viewInfo->Rotation.Yaw * PI / 180.0f;
			freeCamVelocity.X += (cosf(r)) * v.X - sinf(r) * v.X * flySpeed;
			freeCamVelocity.Y += (cosf(r)) * v.X - sinf(r) * v.X * flySpeed;
			freeCamVelocity.Z += v.Z * flySpeed;
			static HWND hWnd = 0;
			hWnd = FindWindow((L"UnrealWindow"), (L"Fortnite  "));

			if (Utilities::SpoofCall(GetForegroundWindow) == hWnd) {
				if (Utilities::SpoofCall(GetAsyncKeyState, 0x57)) { // W
					Minipulation::FreeCamPosition.Y = Minipulation::FreeCamPosition.Y + freeCamVelocity.Y - options.CamSpeed;

				}
				if (Utilities::SpoofCall(GetAsyncKeyState, 0x41)) { // S
					Minipulation::FreeCamPosition.X = Minipulation::FreeCamPosition.X + freeCamVelocity.X - options.CamSpeed;

				}
				if (Utilities::SpoofCall(GetAsyncKeyState, 0x53)) { // A

					Minipulation::FreeCamPosition.Y = Minipulation::FreeCamPosition.Y + freeCamVelocity.Y + options.CamSpeed;
				}
				if (Utilities::SpoofCall(GetAsyncKeyState, 0x44)) { // D
					Minipulation::FreeCamPosition.X = Minipulation::FreeCamPosition.X + freeCamVelocity.X + options.CamSpeed;

				}
				if (Utilities::SpoofCall(GetAsyncKeyState, VK_SPACE)) { // Space
					Minipulation::FreeCamPosition.Z = Minipulation::FreeCamPosition.Z + freeCamVelocity.Z + options.CamSpeed;
				}
				if (Utilities::SpoofCall(GetAsyncKeyState, VK_LCONTROL)) { // Ctrl
					Minipulation::FreeCamPosition.Z = Minipulation::FreeCamPosition.Z + freeCamVelocity.Z - options.CamSpeed;
				}
			}
			viewInfo->Location = Minipulation::FreeCamPosition;
			//viewInfo->Rotation.Pitch = 
		}

		else {
			freeCamVelocity = { 0 };
		}
		return ret;
	}

	VOID ReloadHook(PVOID arg0, PVOID arg1) {
		if (options.InstantReload && Minipulation::LocalPlayerPawn) {
			auto localPlayerWeapon = ReadPointer(Minipulation::LocalPlayerPawn, Offsets::FortniteGame::FortPawn::CurrentWeapon);
			if (localPlayerWeapon) {
				auto stats = GetWeaponStats(localPlayerWeapon);
				if (stats) {
					auto& reloadTime = *reinterpret_cast<float*>(reinterpret_cast<PBYTE>(stats) + Offsets::FortniteGame::FortBaseWeaponStats::ReloadTime);
					if (reloadTime != 0.01f) {
						originalReloadTime = reloadTime;
						reloadTime = 0.01f;
					}
				}
			}
		}

		ReloadOriginal(arg0, arg1);
	}

	PVOID* Proccess = 0;
}

VOID Main() {
    
    Utilities::CreateConsole();
    extern uint64_t base_address;
    uintptr_t UObjectArray = (uintptr_t)Utilities::FindPattern(("\x48\x8B\x05\x00\x00\x00\x00\x4C\x8D\x3C\xCD"), ("xxx????xxxx"));
    uintptr_t GetNameByIndex = (uintptr_t)Utilities::FindPattern(("\x48\x89\x5C\x24\x00\x55\x56\x57\x48\x8B\xEC\x48\x83\xEC\x30\x8B"), ("xxxx?xxxxxxxxxxx"));
    uintptr_t GetObjectName = (uintptr_t)Utilities::FindPattern(("\x40\x53\x48\x83\xEC\x20\x48\x8B\xD9\x48\x85\xD2\x75\x45\x33\xC0\x48\x89\x01\x48\x89\x41\x08\x8D\x50\x05\xE8\x00\x00\x00\x00\x8B\x53\x08\x8D\x42\x05\x89\x43\x08\x3B\x43\x0C\x7E\x08\x48\x8B\xCB\xE8\x00\x00\x00\x00\x48\x8B\x0B\x48\x8D\x15\x00\x00\x00\x00\x41\xB8\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\xC3\x48\x83\xC4\x20\x5B\xC3\x48\x8B\x42\x18"),  ("xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxxx????xx????x????xxxxxxxxxxxxx"));
    uintptr_t FnFree = (uintptr_t)Utilities::FindPattern(("\x48\x85\xC9\x74\x2E\x53\x48\x83\xEC\x20\x48\x8B\xD9\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x75\x0C"), ("xxxxxxxxxxxxxxxx????xxxxx"));
    FortUpdater* Updater = new FortUpdater();
    if (Updater->Init(UObjectArray, GetObjectName, GetNameByIndex, FnFree))
    { 
    Offsets::Engine::World::Levels = Updater->FindOffset("World", "Levels");
    Offsets::Engine::GameInstance::LocalPlayers = Updater->FindOffset("GameInstance", "LocalPlayers");
    Offsets::Engine::World::OwningGameInstance = Updater->FindOffset("World", "OwningGameInstance");
    Offsets::Engine::Controller::ControlRotation = Updater->FindOffset("Controller", "ControlRotation");
    Offsets::Engine::PlayerController::PlayerCameraManager = Updater->FindOffset("PlayerController", "PlayerCameraManager");
    Offsets::Engine::PlayerController::AcknowledgedPawn = Updater->FindOffset("PlayerController", "AcknowledgedPawn");
    Offsets::Engine::Pawn::PlayerState = Updater->FindOffset("Pawn", "PlayerState");
    Offsets::Engine::Actor::RootComponent = Updater->FindOffset("Actor", "RootComponent");
    Offsets::Engine::Character::Mesh = Updater->FindOffset("Character", "Mesh");
    Offsets::Engine::SceneComponent::RelativeLocation = Updater->FindOffset("SceneComponent", "RelativeLocation");
    Offsets::Engine::SceneComponent::ComponentVelocity = Updater->FindOffset("SceneComponent", "ComponentVelocity");
    Offsets::Engine::StaticMeshComponent::StaticMesh = Updater->FindOffset("StaticMeshComponent", "StaticMesh");
    Offsets::Engine::SkinnedMeshComponent::CachedWorldSpaceBounds = Updater->FindOffset("SkinnedMeshComponent", "CachedWorldSpaceBounds");
    Offsets::FortniteGame::FortPawn::bIsDBNO = Updater->FindOffset("FortniteGame", "bIsDBNO");
    Offsets::FortniteGame::FortPawn::bIsDying = Updater->FindOffset("FortniteGame", "bIsDying");
    Offsets::FortniteGame::FortPlayerStateAthena::TeamIndex = Updater->FindOffset("FortniteGame", "TeamIndex");
    Offsets::FortniteGame::FortPickup::PrimaryPickupItemEntry = Updater->FindOffset("FortniteGame", "PrimaryPickupItemEntry");
    Offsets::FortniteGame::FortItemDefinition::DisplayName = Updater->FindOffset("FortItemDefinition", "DisplayName");
    Offsets::FortniteGame::FortItemDefinition::Tier = Updater->FindOffset("FortItemDefinition", "Tier");
    Offsets::FortniteGame::FortItemEntry::ItemDefinition = Updater->FindOffset("FortniteGame", "ItemDefinition");
    Offsets::FortniteGame::FortPawn::CurrentWeapon = Updater->FindOffset("FortniteGame", "CurrentWeapon");
    Offsets::FortniteGame::FortWeapon::WeaponData = Updater->FindOffset("FortniteGame", "WeaponData");
    Offsets::FortniteGame::FortWeaponItemDefinition::WeaponStatHandle = Updater->FindOffset("FortniteGame", "WeaponStatHandle");
    Offsets::FortniteGame::FortProjectileAthena::FireStartLoc = Updater->FindOffset("FortniteGame", "FireStartLoc");
    Offsets::FortniteGame::FortBaseWeaponStats::ReloadTime = Updater->FindOffset("FortniteGame", "ReloadTime");
    Offsets::FortniteGame::BuildingContainer::bAlreadySearched = Updater->FindOffset("FortniteGame", "bAlreadySearched");
    }
    std::cout << "Engine::World::Levels: 0x" << std::hex << Offsets::Engine::World::Levels << std::endl;
    std::cout << "Engine::GameInstance::LocalPlayers: 0x" << std::hex << Offsets::Engine::GameInstance::LocalPlayers << std::endl;
    std::cout << "Engine::World::OwningGameInstance: 0x" << std::hex << Offsets::Engine::World::OwningGameInstance << std::endl;
    std::cout << "Engine::Controller::ControlRotation: 0x" << std::hex << Offsets::Engine::Controller::ControlRotation << std::endl;
    std::cout << "Engine::PlayerController::PlayerCameraManager: 0x" << std::hex << Offsets::Engine::PlayerController::PlayerCameraManager << std::endl;
    std::cout << "Engine::Pawn::PlayerState: 0x" << std::hex << Offsets::Engine::Pawn::PlayerState << std::endl;
    std::cout << "Engine::Actor::RootComponent: 0x" << std::hex << Offsets::Engine::Actor::RootComponent << std::endl;
    std::cout << "Engine::Character::Mesh: 0x" << std::hex << Offsets::Engine::Character::Mesh << std::endl;
    std::cout << "Engine::SceneComponent::RelativeLocation: 0x" << std::hex << Offsets::Engine::SceneComponent::RelativeLocation << std::endl;
    std::cout << "Engine::SceneComponent::ComponentVelocity: 0x" << std::hex << Offsets::Engine::SceneComponent::ComponentVelocity << std::endl;
    std::cout << "Engine::StaticMeshComponent::StaticMesh: 0x" << std::hex << Offsets::Engine::StaticMeshComponent::StaticMesh << std::endl;
    std::cout << "Engine::SkinnedMeshComponent::CachedWorldSpaceBounds: 0x" << std::hex << Offsets::Engine::SkinnedMeshComponent::CachedWorldSpaceBounds << std::endl;
    std::cout << "FortniteGame::FortPawn::bIsDBNO: 0x" << std::hex << Offsets::FortniteGame::FortPawn::bIsDBNO << std::endl;
    std::cout << "FortniteGame::FortPawn::bIsDying: 0x" << std::hex << Offsets::FortniteGame::FortPawn::bIsDying << std::endl;
    std::cout << "FortniteGame::FortPlayerStateAthena::TeamIndex: 0x" << std::hex << Offsets::FortniteGame::FortPlayerStateAthena::TeamIndex << std::endl;
    std::cout << "FortniteGame::FortPickup::PrimaryPickupItemEntry: 0x" << std::hex << Offsets::FortniteGame::FortPickup::PrimaryPickupItemEntry << std::endl;
    std::cout << "FortniteGame::FortItemDefinition::DisplayName: 0x" << std::hex << Offsets::FortniteGame::FortItemDefinition::DisplayName << std::endl;
    std::cout << "FortniteGame::FortItemDefinition::Tier: 0x" << std::hex << Offsets::FortniteGame::FortItemDefinition::Tier << std::endl;
    std::cout << "FortniteGame::FortItemEntry::ItemDefinition: 0x" << std::hex << Offsets::FortniteGame::FortItemEntry::ItemDefinition << std::endl;
    std::cout << "FortniteGame::FortPawn::CurrentWeapon: 0x" << std::hex << Offsets::FortniteGame::FortPawn::CurrentWeapon << std::endl;
    std::cout << "FortniteGame::FortWeapon::WeaponData: 0x" << std::hex << Offsets::FortniteGame::FortWeapon::WeaponData << std::endl;
    std::cout << "FortniteGame::FortWeaponItemDefinition::WeaponStatHandle: 0x" << std::hex << Offsets::FortniteGame::FortWeaponItemDefinition::WeaponStatHandle << std::endl;
    std::cout << "FortniteGame::FortProjectileAthena::FireStartLoc: 0x" << std::hex << Offsets::FortniteGame::FortProjectileAthena::FireStartLoc << std::endl;
    std::cout << "FortniteGame::FortBaseWeaponStats::ReloadTime: 0x" << std::hex << Offsets::FortniteGame::FortBaseWeaponStats::ReloadTime << std::endl;
    std::cout << "FortniteGame::BuildingContainer::bAlreadySearched: 0x" << std::hex << Offsets::FortniteGame::BuildingContainer::bAlreadySearched << std::endl;
    std::cout << "Offset Dumper by Ticxsy#0001" << std::endl;

    optionsHelper::Initialize();
	// GObjects
	auto addr = Utilities::FindPattern("\x48\x8B\x05\x00\x00\x00\x00\x4C\x8D\x3C\xCD", "xxx????xxxx");
	if (!addr) {
		MessageBox(0, L"Failed To Verify Signature!  Error Code: UtilGO", L"A Fatal Error Occured", MB_OK | MB_ICONERROR);
	}

	Utilities::objects = reinterpret_cast<decltype(Utilities::objects)>(RELATIVE_ADDR(addr, 7));

	// GetObjectName
	addr = Utilities::FindPattern("\x40\x53\x48\x83\xEC\x20\x48\x8B\xD9\x48\x85\xD2\x75\x45\x33\xC0\x48\x89\x01\x48\x89\x41\x08\x8D\x50\x05\xE8\x00\x00\x00\x00\x8B\x53\x08\x8D\x42\x05\x89\x43\x08\x3B\x43\x0C\x7E\x08\x48\x8B\xCB\xE8\x00\x00\x00\x00\x48\x8B\x0B\x48\x8D\x15\x00\x00\x00\x00\x41\xB8\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\xC3\x48\x83\xC4\x20\x5B\xC3\x48\x8B\x42\x18", "xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxxx????xx????x????xxxxxxxxxxxxx");
	if (!addr) {
		MessageBox(0, L"Failed To Verify Signature!  Error Code: UtilGONI", L"A Fatal Error Occured", MB_OK | MB_ICONERROR);
	}

	Utilities::GetObjectNameInternal = reinterpret_cast<decltype(Utilities::GetObjectNameInternal)>(addr);

	// Free
	addr = Utilities::FindPattern("\x48\x85\xC9\x74\x2E\x53\x48\x83\xEC\x20\x48\x8B\xD9\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x75\x0C", "xxxxxxxxxxxxxxxx????xxxxx");
	if (!addr) {
		MessageBox(0, L"Failed To Verify Signature!  Error Code: UtilFI", L"A Fatal Error Occured", MB_OK | MB_ICONERROR);
	}

	Utilities::FreeInternal = reinterpret_cast<decltype(Utilities::FreeInternal)>(addr);

	// CalculateProjectionMatrixGivenView
	addr = Utilities::FindPattern("\xF3\x0F\x5F\x0D\x00\x00\x00\x00\x41\x8B\x41\x08", "xxxx????xxxx");
	if (!addr) {
		MessageBox(0, L"Failed To Verify Signature!  Error Code: UtilCPMGV", L"A Fatal Error Occured", MB_OK | MB_ICONERROR);
	}

	addr -= 0x280;
	DISCORD.HookFunction((uintptr_t)addr, (uintptr_t)Utilities::CalculateProjectionMatrixGivenViewHook, (uintptr_t)&Utilities::CalculateProjectionMatrixGivenView);
	//MH_CreateHook(addr, CalculateProjectionMatrixGivenViewHook, (PVOID*)&CalculateProjectionMatrixGivenView);
	//MH_EnableHook(addr);

	// LineOfSightTo
	addr = Utilities::FindPattern("\x40\x55\x53\x56\x57\x48\x8D\x6C\x24\x00\x48\x81\xEC\x00\x00\x00\x00\x48\x8B\x05\x00\x00\x00\x00\x48\x33\xC4\x48\x89\x45\xE0\x49", "xxxxxxxxx?xxx????xxx????xxxxxxxx");
	if (!addr) {
		MessageBox(0, L"Failed To Verify Signature!  Error Code: UtilLOST", L"A Fatal Error Occured", MB_OK | MB_ICONERROR);
	}

	Utilities::LineOfSightToInternal = reinterpret_cast<decltype(Utilities::LineOfSightToInternal)>(addr);

	 addr = Utilities::FindPattern("\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x3B\x41", "xxx????xxxxxx");
	if (!addr) {
		MessageBox(0, L"Failed to find uWorld", L"Failure", 0);
	}

	Offsets::uWorld = reinterpret_cast<decltype(Offsets::uWorld)>(RELATIVE_ADDR(addr, 7));

	Offsets::Engine::Controller::SetControlRotation = Utilities::FindObject(L"/Script/Engine.Controller.ClientSetRotation");
	if (!Offsets::Engine::Controller::SetControlRotation) {
		MessageBox(0, L"Failed to find SetControlRotation", L"Failure", 0);
	}
	Offsets::LaunchCharacter = Utilities::FindObject(L"/Script/Engine.Character.LaunchCharacter");
	if (!Offsets::LaunchCharacter) {
		MessageBox(0, L"Failed to find LaunchCharacter", L"Failure", 0);
	}

	Offsets::Engine::PlayerState::GetPlayerName = Utilities::FindObject(L"/Script/Engine.PlayerState.GetPlayerName");
	if (!Offsets::Engine::PlayerState::GetPlayerName) {
		MessageBox(0, L"Failed to find GetPlayerName", L"Failure", 0);
	}


	Offsets::Engine::World::OwningGameInstance = 0x188;
	Offsets::Engine::World::Levels = 0x148;
	Offsets::Engine::GameInstance::LocalPlayers = 0x38;
	Offsets::Engine::Player::PlayerController = 0x30;
	Offsets::Engine::PlayerController::AcknowledgedPawn = 0x298;
	Offsets::Engine::Controller::ControlRotation = 0x280;
	Offsets::Engine::Pawn::PlayerState = 0x238;
	Offsets::Engine::Actor::RootComponent = 0x130;
	Offsets::Engine::Character::Mesh = 0x278;
	Offsets::Engine::SceneComponent::RelativeLocation = 0x11C;
	Offsets::Engine::SceneComponent::ComponentVelocity = 0x140;
	Offsets::Engine::StaticMeshComponent::StaticMesh = 0x420;
	Offsets::Engine::SkinnedMeshComponent::CachedWorldSpaceBounds = 0x5A0;
	Offsets::FortniteGame::FortPawn::bIsDBNO = 0x53A;
    Offsets::FortniteGame::FortPawn::bIsDying = 0x520;
	Offsets::FortniteGame::FortPlayerStateAthena::TeamIndex = 0xE60;
	Offsets::FortniteGame::FortPickup::PrimaryPickupItemEntry = 0x290;
	Offsets::FortniteGame::FortItemDefinition::DisplayName = 0x70;
	Offsets::FortniteGame::FortItemDefinition::Tier = 0x54;
	Offsets::FortniteGame::FortItemEntry::ItemDefinition = 0x18;
	Offsets::FortniteGame::FortPawn::CurrentWeapon = 0x588;
	Offsets::FortniteGame::FortWeapon::WeaponData = 0x358;
	Offsets::FortniteGame::FortWeaponItemDefinition::WeaponStatHandle = 0x7B8;
	Offsets::FortniteGame::FortProjectileAthena::FireStartLoc = 0x868;
	Offsets::FortniteGame::FortBaseWeaponStats::ReloadTime = 0xFC;
	Offsets::FortniteGame::BuildingContainer::bAlreadySearched = 0xC49;

	// GetWeaponStats
	Minipulation::GetWeaponStats = reinterpret_cast<decltype(Minipulation::GetWeaponStats)>(Utilities::FindPattern("\x48\x83\xEC\x58\x48\x8B\x91\x00\x00\x00\x00\x48\x85\xD2\x0F\x84\x00\x00\x00\x00\xF6\x81\x00\x00\x00\x00\x00\x74\x10\x48\x8B\x81\x00\x00\x00\x00\x48\x85\xC0\x0F\x85\x00\x00\x00\x00\x48\x8B\x8A\x00\x00\x00\x00\x48\x89\x5C\x24\x00\x48\x8D\x9A\x00\x00\x00\x00\x48\x85\xC9", "xxxxxxx????xxxxx????xx?????xxxxx????xxxxx????xxx????xxxx?xxx????xxx"));

	// ProcessEvent
	DISCORD.HookFunction((uintptr_t)Utilities::FindPattern("\x40\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x81\xEC\x00\x00\x00\x00\x48\x8D\x6C\x24\x00\x48\x89\x9D\x00\x00\x00\x00\x48\x8B\x05\x00\x00\x00\x00\x48\x33\xC5\x48\x89\x85\x00\x00\x00\x00\x8B\x41\x0C\x45\x33\xF6\x3B\x05\x00\x00\x00\x00\x4D\x8B\xF8\x48\x8B\xF2\x4C\x8B\xE1\x41\xB8\x00\x00\x00\x00\x7D\x2A", "xxxxxxxxxxxxxxx????xxxx?xxx????xxx????xxxxxx????xxxxxxxx????xxxxxxxxxxx????xx"), (uintptr_t)Minipulation::ProcessEventHook, (uintptr_t)&Minipulation::ProcessEvent);

	// GetViewPoint
	DISCORD.HookFunction((uintptr_t)Utilities::FindPattern("\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x20\x48\x8B\xD9\x41\x8B\xF0\x48\x8B\x49\x30\x48\x8B\xFA\xE8\x00\x00\x00\x00\xBA\x00\x00\x00\x00\x48\x8B\xC8", "xxxx?xxxx?xxxxxxxxxxxxxxxxxxx????x????xxx"), (uintptr_t)Minipulation::GetViewPointHook, (uintptr_t)&Minipulation::GetViewPoint);

	//CalculateShot
	DISCORD.HookFunction((uintptr_t)Utilities::FindPattern("\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x48\x89\x7C\x24\x18\x4C\x89\x4C\x24\x20\x55\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24\xD0", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"), (uintptr_t)Minipulation::CalculateShotHook, (uintptr_t)&Minipulation::CalculateShot);

	// Reload
	DISCORD.HookFunction((uintptr_t)Utilities::FindPattern("\x40\x55\x53\x57\x41\x56\x48\x8D\xAC\x24\x00\x00\x00\x00\x48\x81\xEC\x00\x00\x00\x00\x0F\x29\xBC\x24\x00\x00\x00\x00", "xxxxxxxxxx????xxx????xxxx????"), (uintptr_t)Minipulation::ReloadHook, (uintptr_t)&Minipulation::ReloadOriginal);

	const auto pcall_present_discord = Helper::PatternScan(Discord::GetDiscordModuleBase(), "FF 15 ? ? ? ? 8B D8 E8 ? ? ? ? E8 ? ? ? ? EB 10");

	if (!pcall_present_discord)

	const auto poriginal_present = reinterpret_cast<f_present*>(pcall_present_discord + *reinterpret_cast<int32_t*>(pcall_present_discord + 0x2) + 0x6);
	
	auto presentSceneAdress = Helper::PatternScan(Discord::GetDiscordModuleBase(),
		"48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 41 8B F8");
	DISCORD.HookFunction(presentSceneAdress, (uintptr_t)hk_present, (uintptr_t)&PresentOriginal);
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        Main();
    }
    return TRUE;
}