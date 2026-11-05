#include "cvDevices.h"

#include <windows.h>
#include <dshow.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

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
