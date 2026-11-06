#include "blobModel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
	// Um decimo do quadro e o que um corpo percorre entre dois frames quando
	// ainda e plausivelmente o mesmo corpo: a andar depressa, a 30 fps, ele anda
	// muito menos que isto. Acima disso nao e o mesmo corpo deslocado - e outro
	// que apareceu no sitio dele, e um id falso vale pior que nenhum id.
	const float CV_BLOB_MATCH = 0.10f;
}

// =========================
BlobRegistry::BlobRegistry() {
	reset();
}

// =========================
void BlobRegistry::reset() {

	// tudo a zero: o frag so le os primeiros count() slots, mas um slot sujo que
	// fosse lido por engano seria uma silhueta inventada no canto do quadro
	std::memset(boxes_, 0, sizeof(boxes_));
	std::memset(ids_, 0, sizeof(ids_));
	std::memset(ages_, 0, sizeof(ages_));
	std::memset(pcx_, 0, sizeof(pcx_));
	std::memset(pcy_, 0, sizeof(pcy_));
	std::memset(pids_, 0, sizeof(pids_));
	std::memset(page_, 0, sizeof(page_));
	std::memset(taken_, 0, sizeof(taken_));
	for (int i = 0; i < CV_MAX_BLOBS; i++) srcs_[i] = ofRectangle();

	n_ = 0;
	pn_ = 0;
	nextId_ = 1;
	matched_ = 0;
	born_ = 0;
}

// =========================
void BlobRegistry::beginFrame() {

	n_ = 0;
	matched_ = 0;
	born_ = 0;
	std::memset(taken_, 0, sizeof(taken_));
}

// =========================
int BlobRegistry::matchId(float cx, float cy, int forced, int& ageOut) {

	if (forced > 0) {
		// o sintetico ja tem identidade: o indice do rect e o corpo, nao ha
		// centro nenhum a procurar
		for (int j = 0; j < pn_; j++) {
			if (!taken_[j] && pids_[j] == forced) {
				taken_[j] = true;
				ageOut = page_[j] + 1;
				matched_++;
				return forced;
			}
		}
		born_++;
		ageOut = 0;
		if (forced >= nextId_) nextId_ = forced + 1;
		return forced;
	}

	// o mais proximo dentro do limiar, nao o primeiro que passa: dois corpos que
	// se cruzam tem de sair pelos ids de sempre, senao o piscar de um glitch
	// trocaria de corpo a meio do gesto
	int best = -1;
	float bestD = CV_BLOB_MATCH * CV_BLOB_MATCH;
	for (int j = 0; j < pn_; j++) {
		if (taken_[j]) continue;
		const float dx = cx - pcx_[j];
		const float dy = cy - pcy_[j];
		const float d = dx * dx + dy * dy;
		if (d < bestD) { bestD = d; best = j; }
	}

	if (best < 0) {
		born_++;
		ageOut = 0;
		return nextId_++;
	}

	taken_[best] = true;
	ageOut = page_[best] + 1;
	matched_++;
	return pids_[best];
}

// =========================
int BlobRegistry::push(const CvBlobBox& box, const ofRectangle& src, int idHint) {

	// as duas rotas ja cortam em CV_MAX_BLOBS antes de chegar aqui; esta linha e
	// o unico motivo pelo qual um chamador novo nao escreve fora de um array fixo
	if (n_ >= CV_MAX_BLOBS) return -1;

	int age = 0;
	const int id = matchId(0.5f * (box.x0 + box.x1), 0.5f * (box.y0 + box.y1), idHint, age);

	const int i = n_;
	n_++;
	boxes_[i] = box;
	srcs_[i] = src;
	ids_[i] = id;
	ages_[i] = age;
	return i;
}

// =========================
void BlobRegistry::endFrame() {

	// o unico sitio que publica os centros: enquanto o frame nao fecha, o frame
	// seguinte nao pode comecar a casar contra slots a meio escrever
	pn_ = n_;
	for (int i = 0; i < pn_; i++) {
		pcx_[i] = 0.5f * (boxes_[i].x0 + boxes_[i].x1);
		pcy_[i] = 0.5f * (boxes_[i].y0 + boxes_[i].y1);
		pids_[i] = ids_[i];
		page_[i] = ages_[i];
	}
}

// =========================
const CvBlobBox& BlobRegistry::boxAt(int i) const { return boxes_[i]; }
const ofRectangle& BlobRegistry::srcAt(int i) const { return srcs_[i]; }
int BlobRegistry::idAt(int i) const { return ids_[i]; }
int BlobRegistry::ageAt(int i) const { return ages_[i]; }

// =========================
int BlobRegistry::verifyFrag(const std::string& fragText, std::string& note) {

	note.clear();

	// A uniao dos dois lados do contrato, conferida uma vez no setup: se o frag
	// declarar outros slots, o upload de 32 ou corta silhuetas ou le lixo; se nao
	// declarar o uBlobs por este nome, o uniform nao existe e a tela nao e o
	// rastreado de ninguem. Os tres estados separam "nao sei" de "sei que esta
	// errado" porque em cena a primeira e uma duvida e a segunda e um incendio.
	if (fragText.empty()) {
		note = "frag nao lido: contrato de blobs nao verificado";
		return 1;
	}

	static const char KEY[] = "#define CV_MAX_BLOBS";
	const size_t at = fragText.find(KEY);
	if (at == std::string::npos) {
		note = "frag nao define CV_MAX_BLOBS: o C++ nao tem com que conferir";
		return 2;
	}

	size_t i = at + (sizeof(KEY) - 1);
	while (i < fragText.size() && (fragText[i] == ' ' || fragText[i] == '\t')) i++;

	int decl = 0;
	bool algum = false;
	while (i < fragText.size() && fragText[i] >= '0' && fragText[i] <= '9') {
		decl = decl * 10 + (fragText[i] - '0');
		i++;
		algum = true;
	}
	if (!algum) {
		note = "frag nao traz numero depois de CV_MAX_BLOBS";
		return 2;
	}

	if (decl != CV_MAX_BLOBS) {
		char b[192];
		// sprintf_s e do namespace global: e uma extensao da MSVC, nao esta em std
		::sprintf_s(b, sizeof(b),
			"frag declara %d slots, o C++ envia %d: o uBlobs cortaria silhuetas ou leria lixo",
			decl, (int)CV_MAX_BLOBS);
		note = b;
		return 2;
	}

	// o layout e a outra metade do contrato: quatro floats por slot e o par
	// (x0,y0,x1,y1) e o que o ramo le como b.x b.y b.z b.w
	if (fragText.find("vec4 uBlobs[CV_MAX_BLOBS]") == std::string::npos) {
		note = "frag nao declara o uBlobs como vec4[CV_MAX_BLOBS]";
		return 2;
	}

	return 0;
}
