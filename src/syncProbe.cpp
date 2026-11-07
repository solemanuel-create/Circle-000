#include "syncProbe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

	// Um relogio so para as duas maos. `steady_clock` e o unico que o callback do
	// motorista tem direito, e a main thread usa o mesmo nos carimbos dela - o
	// contrario seria subtrair instantes de duas origens diferentes e chamar-lhe
	// uma latencia.
	//
	// A base e a primeira chamada, e e posta por compare_exchange porque o
	// motorista pode ser chamado antes de a main armar nada: quem ganhar a
	// corrida poe a base e devolve zero, e os dois relogios ficam a contar a
	// partir do mesmo instante para sempre.
	std::atomic<long long> gBase(-1);

	long long raw() {
		return (long long)std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	long long desdeBase(long long r) {
		long long esperado = -1;
		if (gBase.compare_exchange_strong(esperado, r)) return 0;
		const long long b = gBase.load();
		return b < 0 ? 0 : r - b;
	}

	const char* NOME[CSYNC_N] = {
		"cam->tela", "swap", "cam->pub", "pub->bloco", "nasce->nota", "nasce->pub"
	};
}

// =========================
CvSyncProbe::CvSyncProbe()
	: ativo_(false)
	, frameUs_(-1)
	, drawUs_(-1)
	, temFrame_(false)
	, pubUs_(0)
	, nFonte_(0)
	, vsync_(false)
	, idadePulso_(3)
	, janelaT0_(0)
	, janelaS_(0.0)
	, esc_(new unsigned short[CAP])
{
	pubUs_.store(0);
	tick_[0] = 0;
	hud_[0] = 0;
	for (int i = 0; i < CSYNC_N; i++) nAntes_[i] = 0;
}

// =========================
void CvSyncProbe::setAtivo(bool a) {

	// armar limpa: uma sonda que se liga no meio de um run tem de dizer o que
	// mediu desde ai, nao desde o inicio do processo com um buraco no meio
	ativo_ = a;
	if (!a) return;

	for (int i = 0; i < CSYNC_N; i++) {
		anel_[i].n.store(0);
		anel_[i].fora = 0;
		nAntes_[i] = 0;
	}
	frameUs_ = -1;
	drawUs_ = -1;
	temFrame_ = false;
	pubUs_.store(0);
	nFonte_ = 0;
	janelaT0_ = agora();
	tick_[0] = 0;
	hud_[0] = 0;
}

// =========================
long long CvSyncProbe::agora() {
	return desdeBase(raw());
}

// =========================
long long CvSyncProbe::de(const std::chrono::steady_clock::time_point& t) {
	return desdeBase((long long)std::chrono::duration_cast<std::chrono::microseconds>(
		t.time_since_epoch()).count());
}

// =========================
void CvSyncProbe::frameChega(long long t) {

	if (!ativo_) return;

	// o carimbo do frame e o unico sitio onde a sonda sabe o que e "agora" para
	// o registo: um corpo nasce neste frame, nao no seguinte
	frameUs_ = t;
	temFrame_ = true;
	nFonte_++;
}

// =========================
void CvSyncProbe::pubFim(long long t) {

	if (!ativo_) return;

	if (temFrame_) anel_[CSYNC_CAM_PUB].escreve(t - frameUs_);
	pubUs_.store(t);
}

// =========================
void CvSyncProbe::drawFim(long long t) {

	if (!ativo_) return;

	// o draw deste frame acabou. O que ainda nao aconteceu e o swap, que vem
	// depois e nao esta nas nossas maos - por isso este canal e declarado como
	// limite inferior e o periodo de troca entra como o passo que falta.
	if (temFrame_) {
		anel_[CSYNC_CAM_TELA].escreve(t - frameUs_);
		temFrame_ = false;
	}
	if (drawUs_ >= 0) anel_[CSYNC_SWAP].escreve(t - drawUs_);
	drawUs_ = t;
}

// =========================
void CvSyncProbe::cbFim(long long t) {

	if (!ativo_) return;

	const long long p = pubUs_.load();
	if (p > 0) anel_[CSYNC_PUB_CB].escreve(t - p);
}

// =========================
void CvSyncProbe::nota(long long nasceUs, long long t) {

	if (!ativo_ || nasceUs <= 0) return;
	anel_[CSYNC_NASCE_CB].escreve(t - nasceUs);
}

// =========================
void CvSyncProbe::notaPublicada(long long nasceUs, long long t) {

	if (!ativo_ || nasceUs <= 0) return;
	anel_[CSYNC_NASCE_PUB].escreve(t - nasceUs);
}

// =========================
void CvSyncProbe::estat(int c, Estat& out) {

	Anel& a = anel_[c];
	long n = a.n.load();
	if (n > CAP) n = CAP;
	out.n = n;
	out.fora = a.fora;
	if (n <= 0) return;

	// a copia vem antes de qualquer ordenacao: o motorista pode estar a escrever
	// agora mesmo, e o que se copia e o que o anel dizia ter naquele instante.
	// Um `unsigned short` alinhado nao se parte em duas metades nesta
	// arquitectura, por isso o que se pode perder e uma amostra do fim da
	// janela, nunca um valor a meio.
	unsigned short* e = esc_.get();
	for (long i = 0; i < n; i++) e[i] = a.v[i];

	unsigned short mn = e[0], mx = e[0];
	for (long i = 1; i < n; i++) {
		if (e[i] < mn) mn = e[i];
		if (e[i] > mx) mx = e[i];
	}
	out.min = mn / 10.0;
	out.max = mx / 10.0;

	// nth_element e o que serve aqui: os dois cortes nao pedem a ordem toda, e
	// ordenar 8192 valores por canal em cada saida seria a propria saida a
	// custar milisegundos no meio da janela que ela esta a medir
	const long im = n / 2;
	const long i9 = (n * 9) / 10 < n ? (n * 9) / 10 : n - 1;
	std::nth_element(e, e + im, e + n);
	out.med = e[im] / 10.0;
	std::nth_element(e, e + i9, e + n);
	out.p90 = e[i9] / 10.0;
}

// =========================
double CvSyncProbe::resumo(long long agoraJanela) {

	if (!ativo_) return 0.0;

	const long long t0 = agora();

	const double janelaUs = (double)(agoraJanela - janelaT0_);
	janelaT0_ = agoraJanela;
	janelaS_ = janelaUs / 1e6;

	long somaN = 0;
	for (int i = 0; i < CSYNC_N; i++) {
		estat(i, est_[i]);
		somaN += est_[i].n;
	}

	// os tres pedidos, compostos a partir das medianas de cada canal. E preciso
	// diz-lo porque a soma das medianas nao e a mediana da soma: o que aqui sai
	// e a conta certa entre os numeros certos, nao uma nova medicao.
	const double telaInf = est_[CSYNC_CAM_TELA].med;
	const double telaSup = telaInf + est_[CSYNC_SWAP].med;
	const double somMeio = est_[CSYNC_CAM_PUB].med + est_[CSYNC_PUB_CB].med;
	const double notaMed = est_[CSYNC_NASCE_CB].med;

	char* p = tick_;
	size_t left = sizeof(tick_);
	int w = snprintf(p, left, "janela %.1f s | frames fonte %ld | vsync %s",
		janelaS_, nFonte_, vsync_ ? "ON" : "OFF");
	if (w > 0 && (size_t)w < left) { p += w; left -= (size_t)w; }

	for (int i = 0; i < CSYNC_N; i++) {
		const Estat& e = est_[i];
		const long d = e.n - nAntes_[i];
		nAntes_[i] = e.n;
		w = snprintf(p, left, " | %s n=%ld(+%ld) fora %ld min %.1f med %.1f p90 %.1f max %.1f",
			NOME[i], e.n, d, e.fora, e.min, e.med, e.p90, e.max);
		if (w > 0 && (size_t)w < left) { p += w; left -= (size_t)w; }
	}

	const double gat = (double)idadePulso_ * est_[CSYNC_SWAP].med;
	w = snprintf(p, left,
		" | COMPOSTO cam->tela %.1f..%.1f | cam->som %.1f(+driver) | nasce->nota %.1f | dos quais gatilho %dxswap=%.1f e transporte=%.1f | soma das medianas, nao a mediana da soma",
		telaInf, telaSup, somMeio, notaMed, idadePulso_, gat, notaMed - gat);
	if (w > 0 && (size_t)w < left) { p += w; left -= (size_t)w; }

	// as duas acusacoes da sonda: uma janela sem amostras num canal nao e um
	// zero, e um canal que nao existiu, e dizer isso e o que evita que o numero
	// ausente seja lido como "zero milisegundos"
	if (nFonte_ == 0)
		snprintf(p, left, " | AVISO: nenhum frame de fonte viva nesta corrida");
	else if (est_[CSYNC_NASCE_CB].n == 0)
		snprintf(p, left, " | AVISO: nenhuma nota saiu pelo motorista: nenhum corpo chegou a idade do gatilho");
	else if (est_[CSYNC_PUB_CB].n == 0)
		snprintf(p, left, " | AVISO: nenhum callback do motorista: a rota nao abriu ou esta muda");

	// a linha do HUD e o resumo dos resumos: o que ele precisa de ver ao centro
	// da sala sao os tres numeros, nao os seis canais. O `fps` e o inverso do
	// periodo de troca medido, e e a unica forma de dizer se foi o vsync ou a
	// camera a mandar no ritmo da janela.
	const double notaP90 = est_[CSYNC_NASCE_CB].p90;
	const double fps = est_[CSYNC_SWAP].med > 0.0 ? 1000.0 / est_[CSYNC_SWAP].med : 0.0;
	snprintf(hud_, sizeof(hud_),
		"cam->tela %.0f..%.0f ms | cam->som %.0f ms | nasce->nota %.0f..%.0f ms | swap %.1f (%.1f fps) | cb %ld/%ld | vsync %s",
		telaInf, telaSup, somMeio, notaMed, notaP90,
		est_[CSYNC_SWAP].med, fps,
		est_[CSYNC_PUB_CB].n, nFonte_, vsync_ ? "ON" : "OFF");

	return (agora() - t0) / 1000.0;
}
