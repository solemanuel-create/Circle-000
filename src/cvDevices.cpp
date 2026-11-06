#include "cvDevices.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <dshow.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
	std::string fromWide(const wchar_t* w) {
		if (!w) return std::string();
		int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 0) return std::string();
		std::string out((size_t)n - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
		return out;
	}
}

std::vector<std::string> cvListCameras() {

	std::vector<std::string> names;

	// O apartmenta fica iniciado de proposito: CoUninitialize aqui derrubaria o
	// estado que o backend DirectShow do OpenCV espera encontrar nesta thread.
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	ICreateDevEnum* devEnum = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void**)&devEnum);
	if (FAILED(hr) || !devEnum) return names;

	IEnumMoniker* monEnum = nullptr;
	hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &monEnum, 0);
	if (SUCCEEDED(hr) && monEnum) {
		IMoniker* moniker = nullptr;
		ULONG fetched = 0;
		while (monEnum->Next(1, &moniker, &fetched) == S_OK && moniker) {
			IPropertyBag* bag = nullptr;
			if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
				(void**)&bag)) && bag) {
				VARIANT var;
				VariantInit(&var);
				if (SUCCEEDED(bag->Read(L"FriendlyName", &var, nullptr))
					&& var.vt == VT_BSTR) {
					names.push_back(fromWide(var.bstrVal));
				} else {
					names.push_back(std::string());
				}
				// VariantClear devolve a BSTR; um SysFreeString aqui seria um
				// segundo free no mesmo bloco
				VariantClear(&var);
				bag->Release();
			}
			moniker->Release();
			moniker = nullptr;
		}
		monEnum->Release();
	}
	devEnum->Release();
	return names;
}

// =========================
std::vector<std::string> cvListCamerasMf() {

	// Mesma chamada que o backend MSMF do OpenCV faz dentro de
	// CVideoCaptureMSMF::Open: atributo de tipo, VIDCAP, MFEnumDeviceSources.
	// Repetir a chamada daqui e o unico jeito de saber em texto qual indice e
	// qual camera, porque cv::VideoCapture nao devolve nome de dispositivo.
	std::vector<std::string> names;

	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(MFStartup(MF_VERSION))) return names;

	IMFAttributes* attrs = nullptr;
	if (SUCCEEDED(MFCreateAttributes(&attrs, 1)) && attrs) {
		if (SUCCEEDED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
			IMFActivate** devices = nullptr;
			UINT32 count = 0;
			if (SUCCEEDED(MFEnumDeviceSources(attrs, &devices, &count)) && devices) {
				// o array e o que o indice do cap.open(idx, CAP_MSMF) aponta
				for (UINT32 i = 0; i < count; i++) {
					if (!devices[i]) { names.push_back(std::string()); continue; }
					// GetAllocatedString e o nome real desta SDK: nao ha
					// GetLocaleString em IMFAttributes nos tres SDKs instalados
					// (22621, 26100, 28000), e o atributo tem sublinhado no meio,
					// MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME. A buffer vem do
					// CoTaskMemAlloc do proprio MF, por isso o free abaixo e o
					// unico certo.
					LPWSTR w = nullptr;
					UINT32 chars = 0;
					if (SUCCEEDED(devices[i]->GetAllocatedString(
						MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &w, &chars)) && w) {
						names.push_back(fromWide(w));
						CoTaskMemFree(w);
					} else {
						names.push_back(std::string());
					}
					devices[i]->Release();
				}
				CoTaskMemFree(devices);
			}
		}
		attrs->Release();
	}

	// startup e shutdown em par: o OpenCV faz os proprios dele quando abre a
	// fonte e a contagem do MF e por referencia, entao desbalancear aqui
	// deixaria o backend sem saber se ja esta de pe
	MFShutdown();
	return names;
}
