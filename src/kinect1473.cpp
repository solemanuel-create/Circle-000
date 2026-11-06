#include "kinect1473.h"

#include <windows.h>
#include <NuiApi.h>

#include <cstdio>
#include <string>

namespace {

	// O mapa de paralaxe e estatico e o SDK pede uma distancia para calcular o
	// deslocamento: 2000 mm e um corpo no meio do palco. Longe demais ou perto
	// demais a borda erra alguns pixels, nunca o centro.
	const USHORT MAP_DEPTH_PACKED = (USHORT)(2000 << NUI_IMAGE_PLAYER_INDEX_SHIFT);

	// As duas dimensoes tem que ser as mesmas do NUI_IMAGE_RESOLUTION aberto
	// abaixo: elas cortam o buffer que o LockRect entrega. Com tamanho errado o
	// cv::Mat nasce vazio, o copiar nao faz nada e o chamador recebe um frame que
	// nao existe.
	const int KINECT_W = 640;
	const int KINECT_H = 480;

	// Em cena nao ha consola aberta: o HRESULT tem que virar frase.
	std::string hrText(HRESULT hr) {
		if (SUCCEEDED(hr)) return "ok";
		if (hr == E_NUI_DEVICE_NOT_CONNECTED) return "sensor 1473 desconectado do USB";
		if (hr == E_NUI_DEVICE_NOT_READY) return "sensor ainda acordando: espere 2 s e aperte F2 de novo";
		if (hr == E_NUI_INSUFFICIENTBANDWIDTH) return "USB sem banda: o 1473 precisa de porta propria";
		if (hr == E_NUI_NOTGENUINE) return "o SDK nao reconhece este hardware";
		if (hr == E_NUI_DEVICE_IN_USE) return "outro programa esta segurando o Kinect";
		if (hr == E_NUI_IMAGE_STREAM_IN_USE) return "stream ja aberto por outro programa";
		if (hr == E_NUI_DATABASE_NOT_FOUND) return "instalacao do SDK v1.8 incompleta";
		if (hr == E_NUI_FEATURE_NOT_INITIALIZED) return "o SDK nao inicializou este stream";
		if (hr == E_NUI_FRAME_NO_DATA) return "nenhum frame no stream";
		char b[32];
		sprintf_s(b, sizeof(b), "erro 0x%08X", (unsigned)hr);
		return b;
	}
}

// =========================
struct Kinect1473::Impl {
	HANDLE hColor = nullptr;
	HANDLE hDepth = nullptr;
	// o mapa se escreve no espaco da COR, entao e o ViewArea do frame de cor que
	// o SDK pede; o de profundidade nao serve e nao foi guardado
	NUI_IMAGE_VIEW_AREA colorViewArea;
	long colorFrames = 0;
	long depthFrames = 0;
	long nullPBits = 0;
	int colorPitch = 0;
	int depthPitch = 0;
	// carimbo da abertura: e dele que sai a taxa medida dos dois streams
	ULONGLONG openTick = 0;
};

// =========================
Kinect1473::Kinect1473()
	: ready_(false), mapOk_(false), W_(KINECT_W), H_(KINECT_H) {
	impl_ = new Impl();
	ZeroMemory(&impl_->colorViewArea, sizeof(impl_->colorViewArea));
	// as duas notas sao reescritas em todo grab e em todo buildMap: reservada a
	// capacidade, a atribuicao troca o conteudo dentro do proprio buffer em vez
	// de pedir memoria ao allocator a cada frame do sensor
	statusNote.reserve(224);
	mapNote.reserve(128);
}

// =========================
Kinect1473::~Kinect1473() {
	close();
	delete impl_;
	impl_ = nullptr;
}

// =========================
bool Kinect1473::open(std::string& note) {

	close();

	W_ = KINECT_W;
	H_ = KINECT_H;

	// A contagem vem antes do initialize de proposito: sem ela o NuiInitialize
	// devolve um erro generico e o HUD nao poderia distinguir "nao ha sensor"
	// de "ha sensor e ele nao abriu".
	int sensors = 0;
	HRESULT hr = NuiGetSensorCount(&sensors);
	if (FAILED(hr)) {
		note = "NuiGetSensorCount: " + hrText(hr);
		return false;
	}
	if (sensors <= 0) {
		note = "SDK v1.8 viu 0 sensores: checar USB, driver e o Kinect Service";
		return false;
	}

	// Cor + profundidade com indice de jogador: o indice nao e usado pela
	// maquina de silhueta, mas o stream empacotado e o unico que o 1473 entrega
	// junto da cor sem pedir o motor de esqueleto.
	hr = NuiInitialize(NUI_INITIALIZE_FLAG_USES_COLOR
		| NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX);
	if (FAILED(hr)) {
		note = "NuiInitialize: " + hrText(hr);
		return false;
	}

	hr = NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480,
		0, 2, nullptr, &impl_->hColor);
	if (FAILED(hr)) {
		note = "stream de cor: " + hrText(hr);
		NuiShutdown();
		return false;
	}

	hr = NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX,
		NUI_IMAGE_RESOLUTION_640x480, 0, 2, nullptr, &impl_->hDepth);
	if (FAILED(hr)) {
		note = "stream de profundidade: " + hrText(hr);
		NuiShutdown();
		impl_->hColor = nullptr;
		return false;
	}

	ready_ = true;
	mapOk_ = false;
	map_.clear();
	mapNote.clear();
	impl_->openTick = GetTickCount64();

	char b[64];
	sprintf_s(b, sizeof(b), " | %d sensor(es)", sensors);
	// nenhuma taxa aqui: o que se mostra no HUD e a medida dos contadores, nao a
	// tabela do SDK. Em 640x480 a cor vem na cadencia da profundidade; o "@15"
	// que esta nota carregava e do 1280x1024.
	note = "Kinect 1473 | NuiApi SDK v1.8 | cor + profundidade 640x480" + std::string(b);
	return true;
}

// =========================
void Kinect1473::close() {

	// NuiShutdown fecha os dois streams junto; nao ha NuiImageStreamClose
	// nesta versao do SDK.
	if (ready_) NuiShutdown();
	ready_ = false;
	mapOk_ = false;
	map_.clear();
	mapNote.clear();
	*impl_ = Impl();
}

// =========================
void Kinect1473::buildMap() {

	// Uma vez por abertura: as extrinsecas do 1473 nao mudam, entao o preco de
	// remapear fica fora do orcamento do frame. O que nao mapear cai na
	// identidade, que e o comportamento antigo sem mapa.
	//
	// A variante AtResolution e obrigatoria e a medicao provou: a versao simples
	// assume a profundidade em 320x240 e o stream aqui e 640x480. No centro
	// (320,240) ela devolvia (605,455) e a AtResolution devolve (319,240); nos
	// cantos a simples extrapolava para (1136,850). Um mapa desses jogava a
	// silhueta para fora do corpo e o HUD ainda dizia "falha 0", porque o SDK
	// aceita a chamada e so erra o espaco.
	const int n = W_ * H_;
	map_.assign(n, 0);

	long inside = 0, outside = 0, failed = 0;

	for (int y = 0; y < H_; y++) {
		for (int x = 0; x < W_; x++) {
			LONG cx = 0, cy = 0;
			HRESULT hr = NuiImageGetColorPixelCoordinatesFromDepthPixelAtResolution(
				NUI_IMAGE_RESOLUTION_640x480, NUI_IMAGE_RESOLUTION_640x480,
				&impl_->colorViewArea, x, y, MAP_DEPTH_PACKED, &cx, &cy);
			if (FAILED(hr)) failed++;
			if (SUCCEEDED(hr) && cx >= 0 && cy >= 0 && cx < W_ && cy < H_) {
				map_[y * W_ + x] = (int)(cy * W_ + cx);
				inside++;
			} else {
				if (SUCCEEDED(hr)) outside++;
				map_[y * W_ + x] = y * W_ + x;
			}
		}
	}

	mapOk_ = inside > (n * 3) / 4;

	char b[128];
	sprintf_s(b, sizeof(b), "paralaxe dentro %ld de %d | fora %ld | falha %ld",
		inside, n, outside, failed);
	mapNote = b;
}

// =========================
int Kinect1473::grab(cv::Mat& color, cv::Mat& depthMm) {

	if (!ready_) return 0;
	int flags = 0;

	// O SDK entrega o frame por ponteiro dele mesmo: quem aponta e o NuiApi,
	// quem devolve e NuiImageStreamReleaseFrame. Copiar sempre, guardar nunca.
	const NUI_IMAGE_FRAME* fr = nullptr;

	// Cor sem espera: quem da o ritmo do loop e a profundidade, com timeout
	// proprio. Frame velho e frame repetido, nao erro.
	if (SUCCEEDED(NuiImageStreamGetNextFrame(impl_->hColor, 0, &fr)) && fr != nullptr) {
		INuiFrameTexture* tex = fr->pFrameTexture;
		NUI_LOCKED_RECT lr;
		ZeroMemory(&lr, sizeof(lr));
		if (tex != nullptr && SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
			// um LockRect que devolve sucesso sem ponteiro nao e frame: contar
			// e nao sinalizar, senao o cvtColor do chamador morre no vazio
			if (lr.pBits != nullptr && lr.Pitch >= W_ * 4) {
				cv::Mat view(H_, W_, CV_8UC4, lr.pBits, (size_t)lr.Pitch);
				view.copyTo(color);
				impl_->colorViewArea = fr->ViewArea;
				impl_->colorPitch = lr.Pitch;
				impl_->colorFrames++;
				flags |= 1;
			} else {
				impl_->nullPBits++;
			}
			tex->UnlockRect(0);
		}
		NuiImageStreamReleaseFrame(impl_->hColor, fr);
		fr = nullptr;
	}

	// 34 ms de espera e os 30 fps do stream de profundidade, sem giro de CPU
	if (SUCCEEDED(NuiImageStreamGetNextFrame(impl_->hDepth, 34, &fr)) && fr != nullptr) {
		INuiFrameTexture* tex = fr->pFrameTexture;
		NUI_LOCKED_RECT lr;
		ZeroMemory(&lr, sizeof(lr));
		if (tex != nullptr && SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
			if (lr.pBits != nullptr && lr.Pitch >= W_ * 2) {
				cv::Mat packed(H_, W_, CV_16UC1, lr.pBits, (size_t)lr.Pitch);
				depthMm.create(H_, W_, CV_16UC1);
				for (int y = 0; y < H_; y++) {
					const unsigned short* src = packed.ptr<unsigned short>(y);
					unsigned short* dst = depthMm.ptr<unsigned short>(y);
					for (int x = 0; x < W_; x++) dst[x] = NuiDepthPixelToDepth(src[x]);
				}
				impl_->depthPitch = lr.Pitch;
				impl_->depthFrames++;
				// uma vez por abertura: close() limpa o mapa, entao reabrir refaz
				if (map_.empty()) buildMap();
				flags |= 2;
			} else {
				impl_->nullPBits++;
			}
			tex->UnlockRect(0);
		}
		NuiImageStreamReleaseFrame(impl_->hDepth, fr);
	}

	// saude por stream: em cena isso e a diferenca entre "o Kinect nao tem cor"
	// e "o Kinect esta la e o color stream esta seco". As duas taxas sao medias
	// desde a abertura, nao uma tabela: e assim que o "@15" antigo foi pego.
	const ULONGLONG tick = GetTickCount64();
	const double ms = (tick > impl_->openTick && impl_->openTick != 0)
		? (double)(tick - impl_->openTick) : 1000.0;
	char b[224];
	sprintf_s(b, sizeof(b),
		"cor %ld f @%.1f fps (pitch %d)  |  profundidade %ld f @%.1f fps (pitch %d)  |  buffer vazio %ld",
		impl_->colorFrames, impl_->colorFrames * 1000.0 / ms, impl_->colorPitch,
		impl_->depthFrames, impl_->depthFrames * 1000.0 / ms, impl_->depthPitch,
		impl_->nullPBits);
	statusNote = b;

	return flags;
}
