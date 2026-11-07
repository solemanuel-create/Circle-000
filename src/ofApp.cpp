#include "ofApp.h"
#include "cvDevices.h"

#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
	const double STALL_SECONDS = 1.5;

	string lower(const string& s) {
		string o = s;
		for (auto& c : o) c = (char)tolower((unsigned char)c);
		return o;
	}

	const char* backendName(int api) {
		if (api == cv::CAP_DSHOW) return "DSHOW";
		if (api == cv::CAP_MSMF) return "MSMF";
		return "?";
	}

	// O log proprio do openFrameworks derruba esta build: ofLogNotice morre na
	// primeira chamada mesmo com o link limpo, provado por marcadores antes e
	// depois de cada etapa. Toda diagnostica sai por stderr, que nao tem buffer
	// e sobrevive ao crash.
	void cv0Log(const char* tag, const string& msg) {
		fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
		fflush(stderr);
	}

	// Um frame isolado nao pode enganar a leitura em cena: o primeiro depois
	// do reseed ou de um ajuste de ISO vale menos que a tendencia.
	double ema(double prev, double now) {
		return prev <= 0.0 ? now : prev * 0.8 + now * 0.2;
	}

	// o passo da sonda de churn: cinco segundos e o bastante para uma taxa nao
	// ser um acaso de dois frames e curto o bastante para caber numa entrada e
	// saida de corpo a vista
	const double CHURN_PASSO_S = 5.0;
}

// =========================
void ofApp::logProbe() {

	// A premissa deste app mudo e de antes do core restaurado: o ofLog.cpp
	// corrompido de 14-06 esta em quarentena na raiz do projeto e o
	// openframeworksLib.lib que este binario linka foi refeito em 05-11, depois da
	// restauracao. Ninguem re-testou desde entao. Cada marcador abaixo sai por
	// stderr, que nao depende do que esta sendo testado: se o processo morrer
	// entre dois marcadores, o par que falta diz em qual chamada ele morreu.
	char buf[8] = { 0 };
	size_t got = 0;
	if (getenv_s(&got, buf, sizeof(buf), "CV0_LOGPROBE") != 0 || got == 0) return;

	const ofLogLevel prev = ofGetLogLevel();
	ofSetLogLevel(OF_LOG_NOTICE);

	// cinco escritas, cada uma isolando um suspeito. O marcador antes e depois e
	// o que diz qual delas derruba o processo: a primeira linha sem par e a
	// chamada que matou.
	cv0Log("probe", "nivel antes da sonda = " + ofToString((int)prev) + " (0 = SILENT)");

	cv0Log("probe", "A1 antes de ofLogError com construtor de duas palavras");
	{ ofLogError("ctrlv0", "sonda A"); }
	cv0Log("probe", "A2 sobreviveu");

	cv0Log("probe", "B1 antes de ofLogError com operator<< (a forma do core)");
	{ ofLogError("ctrlv0") << "sonda B"; }
	cv0Log("probe", "B2 sobreviveu");

	cv0Log("probe", "C1 antes de ofLogNotice (stdout, nao stderr)");
	{ ofLogNotice("ctrlv0") << "sonda C"; }
	fflush(nullptr);
	cv0Log("probe", "C2 sobreviveu");

	cv0Log("probe", "D1 antes do log interno do addon (ofxCvGrayscaleImage::allocate 0x0)");
	{
		ofxCvGrayscaleImage probe;
		probe.allocate(0, 0);
	}
	cv0Log("probe", "D2 sobreviveu");

	cv0Log("probe", "E1 antes do log interno do core (ofImage com arquivo ausente)");
	{
		ofImage tmp;
		const bool carregou = tmp.load("cv0_probe_inexistente.jpg");
		cv0Log("probe", "E1b carregou=" + ofToString(carregou));
	}
	cv0Log("probe", "E2 sobreviveu");

	ofSetLogLevel(prev);
	cv0Log("probe", "nivel restaurado para " + ofToString((int)prev)
		+ " (0 = OF_LOG_SILENT): o resto do processo volta a nao logar");
}

// =========================
void ofApp::setup() {

	logProbe();

	// A sonda de transporte bloqueia a main thread por ate 1,5 s por rota viva
	// e fala em stderr a cada API, antes da camera ligar: se uma rota de audio
	// derrubar o processo, a quebra e visivel separada do cold start do
	// Kinect. Sem CV0_AUDIOPROBE a funcao le a variavel, ve que nao existe e
	// volta sem fazer nada.
	audioProbeRun(audioProbe);

	// O teste de emissao vem logo a seguir pela mesma razao da varredura e com
	// um motivo a mais: a varredura devolveu silencio, e silencio nao se ouve.
	// Aqui o buffer do motorista leva um sinus de 440 Hz a -18 dBFS durante a
	// duracao pedida na variavel. Bloqueia a main thread exatamente por essa
	// duracao e por isso acontece antes de qualquer camera ligar. Sem
	// CV0_AUDIOEMIT a funcao le a variavel, ve que nao existe e volta.
	audioEmitRun();

	// A sonda de MIDI vem depois da de transporte e antes da camera pelo mesmo
	// motivo: cada uma bloqueia a main thread por ~1 s por cabo e fala em
	// stderr no fim do proprio turno. Sem CV0_MIDIPROBE a funcao so le a
	// variavel e volta.
	midiProbeRun(midiProbe);

	// A saida de controle nao e sonda: ela abre em todo inicio e a pergunta que
	// responde e do palco, nao do log. Leva um frame a enumerar e abrir uma porta
	// pela winmm, o que e nada ao lado do cold start de qualquer camera, por isso
	// fica aqui na main e nao numa thread.
	midiOut.setup();

	// Sonda de sincronia (CV0_SYNC). Armada ANTES do motor de sintese por uma
	// razao de relogio e nao de gosto: o contexto do motorista nasce dentro do
	// setup() dele e e so esse contexto que o callback ve, por isso quem quiser
	// a idade dos numeros que ele le tem de la estar quando ele for chamado a
	// primeira vez. Sem a variavel nada aqui acontece - e de proposito nem o
	// ofSetVerticalSync e tocado, porque armar uma medicao nao pode mudar a
	// janela de quem nao a pediu.
	{
		char buf[8] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), "CV0_SYNC") == 0 && got > 0 && buf[0] != '0') {
			syncProbe.setAtivo(true);
			syncProbe.setPulseAge(midiOut.idade());
			audioEngine.setSyncProbe(&syncProbe);
			syncAtiva = true;
			syncProx = CHURN_PASSO_S;

			// As duas corridas pedidas distinguem-se por uma variavel e o
			// padrao e ON, que e o que o openFrameworks faz por omissao. Po-lo
			// explicitamente em vez de o assumir e o que permite a linha dizer
			// a condicao em que rodou, e o que a sonda le de volta do GLFW e o
			// numero - nao o que foi pedido.
			char vb[16] = { 0 };
			size_t gvb = 0;
			bool on = true;
			if (getenv_s(&gvb, vb, sizeof(vb), "CV0_SYNC_VSYNC") == 0 && gvb > 0) {
				const string s = lower(string(vb));
				on = !(s == "off" || s == "0" || s == "n" || s == "false");
			}
			ofSetVerticalSync(on);
			// Nao ha leitura de volta: o OF so expoe o setter, e o glfwSwapInterval
			// nao devolve nada nem guarda o estado em lugar consultavel. O que a
			// linha mostra e o pedido, e a prova de que ele bateu e o canal SWAP.
			syncProbe.setVsync(on);

			char b[192];
			::sprintf_s(b, sizeof(b), "sonda ligada: vsync %s por %s (leitura de volta nao existe na OF), pulso aos %d frames de vida, janela %.0f s",
				syncProbe.vsyncPedido() ? "ON" : "OFF",
				gvb > 0 ? "CV0_SYNC_VSYNC" : "padrao da casa",
				midiOut.idade(), CHURN_PASSO_S);
			cv0Log("sync", b);
		}
	}

	// O motor de sintese vem a seguir pelo mesmo motivo do cabo: abre na main,
	// leva uma fracao de frame a enumerar dispositivos e fala em stderr. Depois
	// dele a rota de audio desta maquina fica tomada para toda a vida do
	// processo, e e por isso que uma segunda instancia teria de dizer "nenhuma
	// rota abriu" e continuar a existir. Nao diz ainda: ela morre a 0xc0000005
	// dentro da abertura ASIO, o que esta medido em DEPENDENCIAS.md sec 8 com o
	// remendo proposto e nao autorizado. Sem CV0_AUDIOENG nada aqui acontece
	// alem de uma leitura de ambiente.
	audioEngine.setup();

	// Sonda de procedencia do nascimento (CV0_CHURN). Nao e uma medicao nova no
	// caminho: liga um contador dentro do registo, e sem a variavel nenhum campo
	// e tocado. Serve a uma pergunta que a contagem de nascimentos nao responde -
	// se o churn de ~22 notas/s e um contorno a piscar sobre o corte ou um corpo
	// a mover-se mais que o limiar de casamento - e as duas doencas tem remedios
	// opostos.
	{
		char buf[8] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), "CV0_CHURN") == 0 && got > 0 && buf[0] != '0') {
			blobs.setChurnProbe(true);
			churnAtiva = true;
			churnProx = CHURN_PASSO_S;
			cv0Log("churn", "sonda ligada: separa vazio / longe / ocupado / forcado");
		}
	}

	// Tabela de limiares (CV0_BLOBMATCH): o numero vai para o registo e a sonda
	// publica-o em cada linha, porque tres corridas que nao dizem em que
	// condicao rodaram sao tres anecdotas. Sem a variavel, ou com um valor
	// fora do razoavel, o registo fica no padrao de sempre e nada muda.
	{
		char buf[32] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), "CV0_BLOBMATCH") == 0 && got > 0) {
			const float m = (float)atof(buf);
			if (m >= 0.02f && m <= 0.60f) {
				blobs.setMatch(m);
				char b[96];
				::sprintf_s(b, sizeof(b), "CV0_BLOBMATCH=%.3f aplicado ao casamento de ids (padrao 0.100)", m);
				cv0Log("blobs", b);
			} else {
				cv0Log("blobs", "CV0_BLOBMATCH fora de 0.02..0.60: fica o padrao 0.100");
			}
		}
	}

	// Area minima por ambiente (CV0_BLOBAREA), para a varredura da
	// fragmentacao: o corte e a unica alavanca que existe hoje contra o
	// contorno-falso, e mexer nele a mao exigiria uma tecla que a tabela
	// diz nao ter. O clamp e o da linha da tabela (0.001..0.05) escrito a
	// palavra, porque uma varredura fora da faixa do painel nao seria
	// reproduzivel por quem esta no palco. Sem a variavel nada e tocado.
	{
		char buf[32] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), "CV0_BLOBAREA") == 0 && got > 0) {
			const float a = (float)atof(buf);
			if (a >= 0.001f && a <= 0.05f) {
				const float antes = params.minAreaPct;
				params.minAreaPct = a;
				char b[128];
				::sprintf_s(b, sizeof(b),
					"CV0_BLOBAREA=%.3f aplicado ao corte (palco %.3f)", a, antes);
				cv0Log("maquina", b);
			} else {
				cv0Log("maquina", "CV0_BLOBAREA fora de 0.001..0.050: fica o palco");
			}
		}
	}

	ofSetWindowTitle("Ctrl-v-0 TRACK");
	ofSetBackgroundColor(0);
	ofDisableArbTex();

	if (!img.load("0013.jpg")) {
		cv0Log("erro", "nao carregou bin/data/0013.jpg");
	}

	shaderOk = fx.load("shadersGL3/blobfx");
	if (!shaderOk) {
		cv0Log("erro", "nao carregou bin/data/shadersGL3/blobfx.(vert|frag)");
	}

	// O contrato entre a EffectTable e o uEffect do frag, lido uma vez no setup.
	// Um ficheiro, uma vez, fora do ciclo: e o que impede a tela de anunciar
	// efeitos que o shader nao executa. Leio por std e nao por ofBuffer porque a
	// rota de log do OF desta build esta sob OF_LOG_SILENT e nao quero depender
	// dela para saber se um ficheiro existe.
	{
		string txt;
		{
			std::ifstream in(ofToDataPath("shadersGL3/blobfx.frag"));
			std::stringstream ss;
			ss << in.rdbuf();
			txt = ss.str();
		}
		fxContractNote.clear();
		if (txt.empty()) {
			fxContract = 1;
			fxContractNote = "frag nao lido: contrato de efeitos nao verificado";
		} else {
			fxContract = effects.verifyFrag(txt, fxContractNote) ? 0 : 2;
		}
		cv0Log("efeitos", fxContract == 0
			? ofToString(effects.rowCount()) + " efeitos, todos com ramo no frag"
			: (fxContract == 1 ? "frag nao lido: contrato nao verificado" : fxContractNote));

		// A segunda metade do mesmo ficheiro: quantos slots de blob o frag aloja.
		// Do lado do C++ o numero e um so (CV_MAX_BLOBS, cvModel.h) e serve para o
		// upload e para o registo; do lado do GLSL e um #define que ninguem pode
		// importar. Se divergirem, a tela deixa de mostrar silhuetas em silencio.
		blobContractNote.clear();
		blobContract = BlobRegistry::verifyFrag(txt, blobContractNote);
		cv0Log("blobs", blobContract == 0
			? ofToString((int)CV_MAX_BLOBS) + " slots, o mesmo dos dois lados"
			: (blobContract == 1 ? "frag nao lido: contrato de blobs nao verificado" : blobContractNote));
	}

	// Sem os nomes das duas enumeracoes nao ha como o menu F1/F3/F4 distinguir
	// a integrada da EOS e da NDI, e muito menos dizer qual numero e qual camera
	// em cada backend. O indice sozinho nao diz nada em cena.
	camNames = cvListCameras();
	for (size_t i = 0; i < camNames.size(); i++) {
		cv0Log("dshow", ofToString((int)i) + " \"" + camNames[i] + "\"");
	}

	camNamesMf = cvListCamerasMf();
	for (size_t i = 0; i < camNamesMf.size(); i++) {
		cv0Log("msmf", ofToString((int)i) + " \"" + camNamesMf[i] + "\"");
	}

	// As listas so conferem se o mesmo conjunto de nomes aparece nas duas na
	// mesma ordem. Quando nao conferem, o numero que o DSHOW chama de 2 nao e a
	// camera que o MSMF chama de 2, e e por isso que a escolha passou a ser por
	// nome dentro da lista do backend que abre.
	{
		bool same = camNames.size() == camNamesMf.size();
		if (same) {
			for (size_t i = 0; i < camNames.size(); i++) {
				if (lower(camNames[i]) != lower(camNamesMf[i])) { same = false; break; }
			}
		}
		cv0Log(same ? "listas" : "aviso", same
			? "DSHOW e MSMF na mesma ordem: " + ofToString((int)camNames.size()) + " fontes"
			: "DSHOW " + ofToString((int)camNames.size()) + " x MSMF "
				+ ofToString((int)camNamesMf.size()) + " e a ordem difere - indice nao troca de lista");
	}

	chooseSourceFromEnv();
	openSource();

	// A linha do HUD troca de conteudo dentro de um buffer que ja tem a
	// capacidade toda: e o que deixa de haver uma alocacao por linha desenhada.
	hudLinha_.reserve(sizeof(hudTxt_));
	hudPeda_.reserve(sizeof(hudTxt_));

	reseed();
}

// =========================
void ofApp::chooseSourceFromEnv() {

	// CV0_SRC escolhe a fonte no setup. Em cena isso e o que permite reabrir o
	// app ja no Kinect ou na EOS sem apalpar tecla no escuro, e e tambem a
	// unica forma de provar uma fonte que exige hardware sem mao no teclado.
	char buf[32] = { 0 };
	size_t got = 0;
	if (getenv_s(&got, buf, sizeof(buf), "CV0_SRC") != 0 || got == 0) return;

	string v = lower(string(buf));
	// o token e uma coluna da tabela de fontes: uma fonte nova nao precisa de
	// um if aqui, precisa de uma linha la
	const CvSourceRow* r = sources.byEnv(v);
	params.source = r ? (CvSource)r->id : SRC_WEBCAM;

	cv0Log("env", "CV0_SRC=" + v + " -> " + string(sources.name(params.source)));
}

// =========================
int ofApp::pickIndex(const vector<string>& list, const string& needle) {

	// deviceId e a reserva do operador e agora significa "o indice desta lista",
	// nao um numero solto: com o backend na mao, a camera apontada e a camera
	// que aquele backend entrega naquele numero.
	if (params.deviceId >= 0) {
		return params.deviceId < (int)list.size() ? params.deviceId : -1;
	}

	string n = lower(needle);
	for (size_t i = 0; i < list.size(); i++) {
		string nm = lower(list[i]);
		if (n.empty()) {
			// integrada: o primeiro que nao seja um filtro virtual conhecido.
			// Nome vazio e leitura que falhou, nao e camera - e o heuristico
			// chamaria de integrada uma entrada sem nome.
			if (nm.empty()) continue;
			if (nm.find("eos") == string::npos && nm.find("ndi") == string::npos
				&& nm.find("virtual") == string::npos && nm.find("obs") == string::npos) {
				return (int)i;
			}
		} else if (nm.find(n) != string::npos) {
			return (int)i;
		}
	}
	return -1;
}

// =========================
bool ofApp::openIndex(int idx, int api) {

	// Um backend por chamada. Quem segura o numero e o chamador, e o numero que
	// chegou aqui veio da lista deste api - e o que impede o MSMF de abrir a
	// camera que o nome do DSHOW prometeu.
	// O tamanho vai no open e no set: nenhum dos dois backends desta build
	// atende o pedido (pedido 640x480, entrega 1280x720 nos dois).
	std::vector<int> req = {
		cv::CAP_PROP_FRAME_WIDTH, params.camW,
		cv::CAP_PROP_FRAME_HEIGHT, params.camH,
		cv::CAP_PROP_FPS, params.camFps
	};

	if (!cap.open(idx, api, req)) return false;

	// buffersize 1 e o que segura a latencia abaixo dos 120 ms do palco
	cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

	// Uma camera que ignora o tamanho pedido nao entrega nada, e o watchdog so
	// acusaria 1.5 s depois. Provamos um frame agora e, se nao vem, pedimos o
	// tamanho que o dispositivo quiser.
	cv::Mat first;
	if (!cap.read(first) || first.empty()) {
		cap.release();
		if (!cap.open(idx, api)) return false;
		cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
		if (!cap.read(first) || first.empty()) return false;
	}

	mFrame = first;
	return true;
}

// =========================
bool ofApp::openSource(bool forcar) {

	// A tecla de fonte que nao muda nada nao deve pagar uma ligacao. No Kinect
	// isso e ate 25 s de sensor morto no meio de um show, e o F2 duplo ja foi
	// visto aqui tres vezes num so processo. A reserva e a fonte parada: com
	// frame parado a mesma tecla vira o reabrir manual, que e o que se quer.
	if (!forcar) {
		const bool viva = (capOk || kinectOk) && !camStalled;
		if (kinectWaking) {
			keyNote = "Kinect acordando: a proxima tecla de fonte nao faria nada";
			cv0Log("fonte", keyNote);
			return true;
		}
		if (params.source == srcAtivo && viva) {
			keyNote = string(sources.name(params.source))
				+ " ja esta ligado e entregando frame: nada a reabrir";
			cv0Log("fonte", keyNote);
			return true;
		}
	}

	closeSource();

	// a linha da fonte diz como o pixel chega, e e isto que decide o caminho: o
	// NuiApi nao tem lista, nao tem backend e nao aceita pedido de tamanho.
	const CvSourceRow& sr = sources.byId(params.source);

	if (sr.transport == TRANS_NUI) {
		// o 1473 nao passa pelo cv::VideoCapture: e NuiApi. A ligacao vai para
		// uma thread porque o servico frio segura a main por 20-25 s, e uma
		// janela congelada no palco nao se distingue de um crash. O join de
		// qualquer ligacao anterior ja foi feito pelo closeSource() acima.
		kinectOpenDone = false;
		kinectOpenResult = false;
		kinectOpenNote.clear();
		kinectWaking = true;
		kinectWakeStart = ofGetElapsedTimef();
		kinectOpens++;
		capNote = "o servico frio leva ate 25 s";
		cv0Log("fonte", capNote);
		kinectThread = std::thread([this]() {
			string note;
			const bool ok = kinect.open(note);
			kinectOpenResult = ok;
			kinectOpenNote = note;
			kinectOpenDone = true;
		});
		return false;
	}

	// O backend decide a lista. Os dois enumeram o mesmo hardware cada um a sua
	// maneira, entao o numero 2 do DSHOW pode ser a camera que o MSMF chama de
	// zero, e abrir com o numero da lista errada entrega ao palco uma camera
	// diferente da que o nome no HUD promete.
	const int primeiro = params.dshowFirst ? cv::CAP_DSHOW : cv::CAP_MSMF;
	const int segundo = params.dshowFirst ? cv::CAP_MSMF : cv::CAP_DSHOW;

	// o que se procura em cada fonte: null e "nao ha lista" (ja saiu acima),
	// vazio e "a primeira que nao seja virtual", e a linha da tabela que diz
	// qual dos dois
	string needle = sr.needle ? sr.needle : "";

	int abertoIdx = -1;
	int abertoApi = 0;
	string abertoNome;
	int ultimaIdx = -1;
	string ultimoNome;

	for (int t = 0; t < 2; t++) {
		const int api = (t == 0) ? primeiro : segundo;
		const vector<string>& list = (api == cv::CAP_MSMF) ? camNamesMf : camNames;
		if (list.empty()) continue;

		int idx = pickIndex(list, needle);
		// So a integrada tem reserva. Cair no zero com o F3 ou o F4 daria ao
		// palco a webcam do laptop achando que e a EOS ou o NDI.
		if (idx < 0 && sr.fallbackIndex0) idx = 0;
		if (idx < 0 || idx >= (int)list.size()) continue;

		ultimaIdx = idx;
		ultimoNome = list[idx];

		if (openIndex(idx, api)) {
			abertoIdx = idx;
			abertoApi = api;
			abertoNome = list[idx];
			break;
		}
		// tentativa falha pode deixar o cap segurando o dispositivo: soltar
		// antes de chamar o outro backend, senao o segundo herda uma porta ocupada
		cap.release();
	}

	if (abertoIdx < 0) {
		capNote = (ultimaIdx < 0)
			? string("nenhum dispositivo para \"") + sources.name(params.source) + "\""
			: "\"" + ultimoNome + "\" (dev " + ofToString(ultimaIdx) + ") nao abriu em "
				+ string(backendName(primeiro)) + " nem " + string(backendName(segundo));
		cv0Log("erro", capNote);
		return false;
	}

	capOk = true;
	capIndex = abertoIdx;
	capApi = abertoApi;
	srcAtivo = params.source;
	mask.invalidateBackground();
	frameNew = true;
	lastNewFrame = ofGetElapsedTimef();

	// o nome vem da lista do backend que abriu, nao da outra: e o numero que o
	// cap.get() devolve e o que o DRIVER diz que esta ajustado; o mFrame e o que
	// ele realmente entrega. Em cena a diferenca e o bug.
	//
	// "pedido" aqui foi renomeado no P2 porque a tabela de parametros tem uma
	// linha "modo pedido" que e outra coisa: o valor que o nosso codigo manda ao
	// cv::VideoCapture. Os dois podem discordar - e discordam nesta camera, onde
	// o codigo pede 640x480 e o driver responde 1280x720 - e dois nomes iguais
	// para duas coisas diferentes e exatamente o que a tabela existe para matar.
	capName = abertoNome;
	capNote = capName + " | " + string(backendName(capApi)) + " #" + ofToString(capIndex)
		+ " | entrega " + ofToString(mFrame.cols) + "x" + ofToString(mFrame.rows)
		+ " | driver diz " + ofToString((int)cap.get(cv::CAP_PROP_FRAME_WIDTH)) + "x"
		+ ofToString((int)cap.get(cv::CAP_PROP_FRAME_HEIGHT)) + "@"
		+ ofToString((int)cap.get(cv::CAP_PROP_FPS));
	cv0Log("fonte", capNote);
	return true;
}

// =========================
void ofApp::closeSource() {

	// enquanto acorda, o NuiInitialize pertence a thread: esperar aqui evita
	// duas coisas piores - um Kinect aberto sem dono quando a troca de fonte
	// cai no meio dos 25 s, e o ofApp morrer com a thread ainda rodando
	if (kinectThread.joinable()) kinectThread.join();
	kinectWaking = false;
	kinectOpenDone = false;
	kinectOpenResult = false;

	if (cap.isOpened()) cap.release();
	kinect.close();
	kinectOk = false;
	capOk = false;
	capIndex = -1;
	capApi = 0;
	srcAtivo = -1;
	// o envelope de uma fonte nao pode herdar para a outra: o Kinect que chega
	// com a memoria da webcam comecaria o palco ja aceso
	sig.reset();
	haveCent = false;
	capName.clear();
	mask.invalidateBackground();
	// os ids de uma fonte nao valem para a outra: o fundo e os corpos mudam juntos
	blobs.reset();
	frameNew = false;
	if (texOk) {
		texLive.clear();
		texOk = false;
	}
}

// =========================
void ofApp::exit() {

	// OF destroi o ofApp depois de fechar a janela: sem este join a thread de
	// ligacao poderia acordar dentro de um objeto que ja nao existe
	closeSource();

	// a porta MIDI e do processo, e um cabo aberto que nao se fecha e um cabo
	// que a proxima instancia nao consegue abrir
	midiOut.close();

	// o mesmo, e mais forte, para a rota de audio: um stream ASIO aberto que
	// fica vivo depois do fecho da janela deixa a proxima instancia muda, e o
	// motorista continua a chamar um callback cujo contexto ja foi destruido
	audioEngine.close();
}

// =========================
void ofApp::update() {

	// a thread de ligacao voltou: a adocao acontece aqui, na main, porque e
	// daqui que o resto do app le kinectOk e capNote
	if (kinectWaking && kinectOpenDone) {
		kinectWaking = false;
		kinectOk = kinectOpenResult;
		capNote = kinectOpenNote;
		if (kinectOk) {
			mask.invalidateBackground();
			lastNewFrame = ofGetElapsedTimef();
		}
		cv0Log(kinectOk ? "fonte" : "aviso", capNote);
		cv0Log("tempo", "Kinect acordou em " + ofToString(ofGetElapsedTimef() - kinectWakeStart, 1) + " s");
		if (kinectOk) srcAtivo = SRC_KINECT;
	}

	origin = CvFrameOrigin();
	if (capOk || kinectOk) {
		double t0 = ofGetElapsedTimeMicros();

		// O par de instantes da captura e o que da nome ao ciclo de entrega do
		// setimo canal: a duracao do grab bloqueante e a unica coisa que diz, sem
		// o carimbo do driver, se a fila do backend estava vazia quando a chamada
		// comecou - e esse carimbo esta build do OpenCV nao expoe. Custa duas
		// chamadas de relogio por frame, e so quando a medicao foi pedida.
		const long long c0 = syncAtiva ? CvSyncProbe::agora() : 0;

		int kFlags = 0;
		if (kinectOk) kFlags = kinect.grab(kColor, kDepth);
		else frameNew = cap.read(mFrame) && !mFrame.empty();

		const long long c1 = syncAtiva ? CvSyncProbe::agora() : 0;

		// So a rota do cv::VideoCapture tem fila de backend. O 1473 entrega pelo
		// NuiApi com timeout proprio, e la nao ha idade de fila para acusar: o
		// canal fica sem amostras e a linha diz isso em vez de dizer zero.
		if (syncAtiva && !kinectOk && frameNew) syncProbe.leituraFeita(c0, c1);

		// no Kinect os dois streams tem ritmos diferentes (cor 15, profundidade
		// 30): a maquina manda no ciclo e a cor e bem-vinda quando chegar
		const bool haveColor = kinectOk ? (kFlags & 1) != 0 : frameNew;
		const bool haveFrame = kinectOk ? (kFlags & 2) != 0 : frameNew;

		// O carimbo da chegada e o instante em que os pixels do sensor estao
		// nesta process e nao um numero do driver: tudo o que a sonda mede a
		// partir daqui e o que a peca faz com eles, e a espera do sensor ja tem
		// casa propria no `espera camera` do HUD
		if (syncAtiva && (haveColor || haveFrame)) syncProbe.frameChega(CvSyncProbe::agora());

		double t1 = ofGetElapsedTimeMicros();

		if (haveColor) {
			if (kinectOk) cv::cvtColor(kColor, mRgb, cv::COLOR_BGRA2RGB);
			else cv::cvtColor(mFrame, mRgb, cv::COLOR_BGR2RGB);
			uploadTexture();
		}
		if (haveFrame) {
			lastNewFrame = ofGetElapsedTimef();
			frameNew = true;
			// as duas rotas entregam a mesma coisa: um CV_8UC1 de maskW x maskH
			// e uma lista de contornos. O que muda e a origem do pixel.
			if (kinectOk) mask.buildDepth(kDepth, kinect.map(), params, space);
			else mask.buildMotion(mRgb, params, space);
		}

		double t2 = ofGetElapsedTimeMicros();
		// dos tres numeros so um e nosso: a espera pela camera e o orcamento
		// inteiro, o rastreio cabe dentro com folga
		readMs = ema(readMs, (t1 - t0) / 1000.0);
		trackMs = ema(trackMs, (t2 - t1) / 1000.0);
		cycleMs = ema(cycleMs, ofGetLastFrameTime() * 1000.0);
		camStalled = (ofGetElapsedTimef() - lastNewFrame) > STALL_SECONDS;
		// o fato medido, uma vez: a fonte entrega frame e a textura existe. As
		// duas perguntas do frame nascem daqui e de mais nada - hoje elas tem a
		// mesma resposta, e e justamente porque ela e escrita uma vez so que
		// elas nao podem divergir no meio de um frame
		const bool viva = !camStalled && texOk;
		origin.tracksSource = viva;
		origin.pixelsFromSource = viva;
	} else {
		camStalled = false;
	}

	// o espaco do shader e o espaco do que vai ser desenhado, nao um numero
	// escolhido a parte: a chamada recebe a mesma funcao que o draw usa
	ofBaseDraws* s = drawSource();
	space.setSource(s ? (int)s->getWidth() : 0, s ? (int)s->getHeight() : 0);
	if (!space.hasSource()) return;

	space.updateFit(ofGetWidth(), ofGetHeight());

	float dt = ofGetLastFrameTime();

	// o barramento antes do empurrao: os retangulos que vao para o shader ja
	// nascem com o crescimento do envelope, sena a imagem atrasaria um frame
	double s0 = ofGetElapsedTimeMicros();
	updateSignals(dt);
	applyGains();
	sigMs = ema(sigMs, (ofGetElapsedTimeMicros() - s0) / 1000.0);

	// O carimbo que os nascimentos deste frame levam. E tirado aqui, logo antes
	// do empurrao e depois da maquina ter fechado os contornos, porque
	// "nascimento" para a peca e o instante em que um id novo passou a existir
	// no registo - nao o instante em que o pixel dele chegou ao sensor, que ja
	// tem canal proprio.
	if (syncAtiva) blobs.setSyncFrameUs(CvSyncProbe::agora());

	if (origin.tracksSource) pushCameraBlobs();
	else pushSyntheticBlobs(dt);

	// O cabo depois do registo: um pulso e um corpo com idade zero, e a idade so
	// existe a partir do momento em que o frame foi empurrado. O valor que sai e
	// o mesmo rMaster que o render usou neste frame, nao um segundo calculo.
	double m0 = ofGetElapsedTimeMicros();
	midiOut.send(sig, rMaster, blobs);
	midiMs = ema(midiMs, (ofGetElapsedTimeMicros() - m0) / 1000.0);

	// O motor depois do cabo pela mesma razao: os dois leem o registo ja
	// empurrado e o mesmo rMaster, e a idade que dispara o pulso e a mesma
	// numero, lida do cabo. Publicar nao e sintetizar: esta linha e doze stores
	// atomicos e a fila dos nascimentos; a conta acontece na thread do
	// motorista e o custo dela sai no tick de cinco em cinco segundos.
	double a0 = ofGetElapsedTimeMicros();
	audioEngine.publish(sig, rMaster, blobs, midiOut.idade());
	// O carimbo e logo a seguir ao publish e nao depois do tick: o que o
	// motorista le sao os atomicos que esta linha deixou, e o tick e a nossa
	// propria saida, que nao pertence ao caminho do som
	if (syncAtiva) syncProbe.pubFim(CvSyncProbe::agora());
	audioEngine.tick();
	audioMs = ema(audioMs, (ofGetElapsedTimeMicros() - a0) / 1000.0);

	if (churnAtiva) churnDump();
	if (syncAtiva) syncDump();
}

// =========================
void ofApp::churnDump() {

	// A conta sai de cinco em cinco segundos. Cinco porque e o suficiente para
	// uma taxa nao ser um acaso de dois frames, e curto o bastante para um corpo
	// que entra e sai ser visto dentro de uma janela. A medicao e a diferenca
	// para a fotografia anterior, nao o acumulado: o que se quer e o que esta a
	// acontecer nesta sala agora.
	const double agora = ofGetElapsedTimef();
	if (agora < churnProx) return;
	const double janela = agora - churnUltima;
	churnUltima = agora;
	churnProx = agora + CHURN_PASSO_S;

	const CvChurnProbe& c = blobs.churn();
	const long dNasc = c.nascidos - churnAntes.nascidos;
	const long dVazio = c.porVazio - churnAntes.porVazio;
	const long dLonge = c.porLonge - churnAntes.porLonge;
	const long dOcup = c.porOcupado - churnAntes.porOcupado;
	const long dForc = c.porForcado - churnAntes.porForcado;
	const long dFrames = c.frames - churnAntes.frames;
	const long dSem = c.framesSemContorno - churnAntes.framesSemContorno;
	const long dLivre = c.longeLivre - churnAntes.longeLivre;
	const long dTomado = c.longeTomado - churnAntes.longeTomado;
	const long dVive = c.pulsoVive - churnAntes.pulsoVive;
	const long dMorre = c.pulsoMorre - churnAntes.pulsoMorre;
	const long dSozinho = c.pulsoSozinho - churnAntes.pulsoSozinho;
	const long dPertoE = c.pulsoPertoEstab - churnAntes.pulsoPertoEstab;
	const long dPertoN = c.pulsoPertoNovo - churnAntes.pulsoPertoNovo;
	const long dFila = c.pulsoFilaCheia - churnAntes.pulsoFilaCheia;
	const long dSaidos = c.pulsoSaidos - churnAntes.pulsoSaidos;

	// a unica conta que prova que o ledger do pulso roda: nascimentos menos os
	// forcados tem de ser exatamente o que ja se resolveu, mais o que esta em
	// fila, mais o que a fila deitou fora. Se o resolver nunca correr, os vivos
	// e os mortos ficam em zero e a fila estoura em 64 - a linha acusa-se so.
	// O unico estado em que sobra um numero e um reseed no meio da janela, e ele
	// diz que foi isso.
	//
	// Com a curva o encerramento mudou de mao: um nascimento so sai da fila por
	// uma de duas portas, `morre` (o id desapareceu antes do ultimo degrau) ou
	// `saidos` (percorreu a curva toda). `vive` nao e terminal - e o degrau dos
	// 8 frames, e quem la chegou e continua vivo ate ao 16 conta-se nas duas
	// contas, por isso somar vive aqui era contar tres vezes o mesmo corpo.
	const long desc = (c.nascidos - c.porForcado)
		- (c.pulsoMorre + c.pulsoSaidos + c.pulsoPendentes + c.pulsoFilaCheia);

	// a media do "longe" e a diferenca das somas sobre a diferenca das contas,
	// nunca a soma acumulada sobre um numero de frames que ja nao e este
	const double med = dLonge > 0
		? (c.somaLonge - churnAntes.somaLonge) / (double)dLonge : 0.0;

	// os degraus da curva sao a pergunta que o N do debounce responde: quantos
	// nascimentos sao ainda o mesmo id a uma idade de 1, 2, 3, 4, 8 e 16 frames.
	// Um degrau que cai a pique entre 1 e 2 e um corpo que pisca no corte; um
	// que se mantem ate ao 16 e um corpo que estava la e o gatilho dos 8
	// frames so atrasou.
	//
	// Os rotulos saem do proprio CURVA_IDADE, nao de uma string escrita a mao:
	// os baldes do "longe" dao-se ao luxo de rotulos fixos porque la o que
	// muda e a borda, que tambem esta a mao no fonte. Aqui os numeros sao uma
	// tabela no .cpp e um rotulo fixo no printf desynchronizava-se dela no
	// silencio - a linha imprimiria "4:" sobre o degrau dos 3 frames.
	char curvaSeg[128];
	curvaSeg[0] = '\0';
	int off = 0;
	long dCurva[CvChurnProbe::CURVA_N];
	for (int k = 0; k < CvChurnProbe::CURVA_N && off < (int)sizeof(curvaSeg) - 24; k++) {
		dCurva[k] = c.curva[k] - churnAntes.curva[k];
		char pedaco[32];
		::sprintf_s(pedaco, sizeof(pedaco), "%s%d:%ld", k == 0 ? "" : " ",
			CvChurnProbe::CURVA_IDADE[k], dCurva[k]);
		const size_t len = strlen(pedaco);
		memcpy(curvaSeg + off, pedaco, len);
		off += (int)len;
		curvaSeg[off] = '\0';
	}

	::sprintf_s(churnNota, sizeof(churnNota),
		"limiar %.3f | janela %.1f s | nasc %ld (%.1f/s) vazio %ld longe %ld ocupado %ld forcado %ld"
		" | longe livre %ld tomado %ld"
		" | frames %ld sem-contorno %ld | longe medio %.3f"
		" | baldes 15:%ld 25:%ld 50:%ld 100:%ld inf:%ld"
		" | pulso %ldf x %.2f: vive %ld morre %ld"
		" | dos vivos: sozinho %ld perto-velho %ld perto-novo %ld"
		" | curva %s saidos %ld"
		" | fila %ld pend %ld desc %ld",
		blobs.match(), janela, dNasc, dNasc / (janela > 0.001 ? janela : 0.001),
		dVazio, dLonge, dOcup, dForc,
		dLivre, dTomado,
		dFrames, dSem, med,
		c.longe[0] - churnAntes.longe[0], c.longe[1] - churnAntes.longe[1],
		c.longe[2] - churnAntes.longe[2], c.longe[3] - churnAntes.longe[3],
		c.longe[4] - churnAntes.longe[4],
		(long)CvChurnProbe::PULSO_N, CvChurnProbe::PULSO_X,
		dVive, dMorre,
		dSozinho, dPertoE, dPertoN,
		curvaSeg, dSaidos,
		dFila, c.pulsoPendentes, desc);

	cv0Log("churn", churnNota);

	churnAntes = c;
}

// =========================
void ofApp::syncDump() {

	// A sincronia nao tem janela propria: cinco segundos e o passo que a casa
	// ja usa para o churn, e duas sondas com relogios diferentes numa so corrida
	// sao duas narrativas que nao se podem somar. O que aqui se imprime e o que
	// a sonda calculou la dentro, uma vez por janela, porque ordenar oito mil
	// valores no caminho do desenho seria a medicao a comer-se a si propria.
	const double agora = ofGetElapsedTimef();
	if (agora < syncProx) return;
	syncProx = agora + CHURN_PASSO_S;

	const double custo = syncProbe.resumo(CvSyncProbe::agora());

	char b[128];
	::sprintf_s(b, sizeof(b), "custo da saida %.3f ms (%d canais, nth_element sobre ate %d amostras)",
		custo, (int)CSYNC_N, CvSyncProbe::CAP);
	cv0Log("sync", b);
	cv0Log("sync", syncProbe.tickNota());
}

// =========================
void ofApp::uploadTexture() {

	if (mRgb.empty()) return;

	if (!texOk || texLive.getWidth() != mRgb.cols || texLive.getHeight() != mRgb.rows) {
		texLive.allocate(mRgb.cols, mRgb.rows, GL_RGB);
		texOk = true;
	}
	texLive.loadData(mRgb.data, mRgb.cols, mRgb.rows, GL_RGB);
}

// =========================
void ofApp::updateSignals(float dt) {

	// Um caminho so para as duas fontes e para o sintetico: o que elas tem em
	// comum e a maquina de contornos, ou os retangulos que a substituem. Nada
	// aqui sabe se o pixel veio do Kinect, da webcam ou do fallback.
	const int n = origin.tracksSource
		? MIN((int)mask.blobs().size(), CV_MAX_BLOBS)
		: (int)rects.size();

	// tudo em coordenada normalizada: um sinal que se mede em pixels mudaria de
	// significado a cada z/x e a cada troca de resolucao de fonte
	float area = 0.0f;
	float cxs = 0.0f, cys = 0.0f;
	float zone[CV_ZONES] = { 0.0f };

	for (int i = 0; i < n; i++) {
		float x0, y0, w, h;
		if (origin.tracksSource) {
			const ofRectangle& b = mask.blobs()[i].boundingRect;
			x0 = space.maskToNormX(b.x); y0 = space.maskToNormY(b.y);
			w = space.maskToNormX(b.width); h = space.maskToNormY(b.height);
		} else {
			const ofRectangle& r = rects[i];
			x0 = space.srcToNormX(r.x); y0 = space.srcToNormY(r.y);
			w = space.srcToNormX(r.width); h = space.srcToNormY(r.height);
		}
		const float a = ofClamp(w, 0.0f, 1.0f) * ofClamp(h, 0.0f, 1.0f);
		if (a <= 0.0f) continue;
		area += a;
		const float mx = ofClamp(x0 + w * 0.5f, 0.0f, 0.999f);
		const float my = ofClamp(y0 + h * 0.5f, 0.0f, 0.999f);
		cxs += mx * a;
		cys += my * a;
		const int col = MIN((int)(mx * CV_ZONE_COLS), CV_ZONE_COLS - 1);
		const int row = MIN((int)(my * CV_ZONE_ROWS), CV_ZONE_ROWS - 1);
		zone[row * CV_ZONE_COLS + col] += a;
	}

	// energia e o que a maquina conta, nao o que os contornos resolveram
	// mostrar: um corpo inteiro abaixo do minArea continua ocupando o palco
	float energy = 0.0f;
	if (origin.tracksSource && !mask.bin().empty() && space.maskW > 0 && space.maskH > 0) {
		energy = (float)cv::countNonZero(mask.bin()) / (float)(space.maskW * space.maskH);
	} else {
		energy = area * 2.0f;
	}

	sig.energy = ofClamp(energy, 0.0f, 1.0f);
	// area vezes tres: um corpo que preenche um terco do quadro e o teto, e e
	// assim que dez dedos deixam de valer mais que um tronco
	sig.density = ofClamp(area * 3.0f, 0.0f, 1.0f);
	for (int i = 0; i < CV_ZONES; i++) sig.zone[i] = ofClamp(zone[i] * 3.0f, 0.0f, 1.0f);

	// velocidade do centro de massa em larguras de quadro por segundo. Sem
	// contorno o centro nao existe, e junto com ele deixa de existir o salto
	// que ele causaria no frame seguinte
	sig.speed = 0.0f;
	if (area > 1e-5f && dt > 1e-4f) {
		const ofVec2f c(cxs / area, cys / area);
		if (haveCent) {
			sig.speed = ofClamp(ofDist(c.x, c.y, prevCent.x, prevCent.y) / dt * 2.0f, 0.0f, 1.0f);
		}
		prevCent = c;
		haveCent = true;
	} else {
		haveCent = false;
	}

	// coerencia e o media sobre o desvio das zonas, vezes um fator que faz uma
	// zona sozinha chegar em zero. Sem atividade a conta e indefinida, e
	// indefinido em cena se le como "a sala esta calma" - entao zero explicito.
	float zsum = 0.0f;
	for (int i = 0; i < CV_ZONES; i++) zsum += sig.zone[i];
	if (zsum > 1e-4f) {
		const float mean = zsum / (float)CV_ZONES;
		float var = 0.0f;
		for (int i = 0; i < CV_ZONES; i++) {
			const float d = sig.zone[i] - mean;
			var += d * d;
		}
		var /= (float)CV_ZONES;
		sig.coherence = ofClamp(1.0f - sqrtf(var) / (mean + 1e-5f) * 0.45f, 0.0f, 1.0f);
	} else {
		sig.coherence = 0.0f;
	}

	// o chao do sinal: media movel de dois segundos da energia. E o que separa
	// a sala vazia da sala cheia ha cinco minutos, e o que o cue idle vai ler.
	sig.ambient += (sig.energy - sig.ambient) * (1.0f - expf(-dt / 2.0f));

	sig.energyE.follow(sig.energy, dt);
	sig.densityE.follow(sig.density, dt);
	sig.speedE.follow(sig.speed, dt);
	sig.coherenceE.follow(sig.coherence, dt);
	sig.ambientE.follow(sig.ambient, dt);
	for (int i = 0; i < CV_ZONES; i++) sig.zoneE[i].follow(sig.zone[i], dt);
}

// =========================
void ofApp::applyGains() {

	// Um multiplo para tudo, porque em cena nao se desliga um sinal de cada vez:
	// o g corta o master e o render volta ao que era antes do pacote 4.
	const float m = params.reactive ? params.master : 0.0f;
	rMaster = m;

	rExposure = m * params.gainExposure * sig.energyE.v;
	rGrow = m * params.gainGrow * (0.6f * sig.densityE.v + 0.4f * sig.speedE.v);
	rThresh = m * params.gainThresh * sig.coherenceE.v;
	rDither = m * params.gainDither * sig.speedE.v;
	rSpeed = m * params.gainSpeed * sig.energyE.v;
}

// =========================
void ofApp::pushCameraBlobs() {

	const int total = (int)mask.blobs().size();
	blobs.beginFrame();
	if (total <= 0) {
		// sem corpo o frame fecha vazio: o casamento do frame seguinte comeca do
		// zero em vez de reencontrar ids de ha dois frames
		blobs.endFrame();
		activeBlobs = 0;
		return;
	}

	// os 32 slots do shader vao para os maiores, nao para os primeiros. O vector
	// e membro e so cresce: a ordenacao nao constroi mais um idx(n) por frame.
	int n = total;
	if ((int)blobOrder.size() < n) blobOrder.resize(n);
	for (int i = 0; i < n; i++) blobOrder[i] = i;
	std::sort(blobOrder.begin(), blobOrder.begin() + n, [&](int a, int b) {
		return mask.blobs()[a].area > mask.blobs()[b].area;
		});

	if (n > CV_MAX_BLOBS) n = CV_MAX_BLOBS;

	// o contorno vive na maquina reduzida e o shader quer 0..1; o retangulo da
	// sobreposicao quer pixel da fonte, e os dois voltam do mesmo par de
	// normalizados
	const float g = 1.0f + rGrow;
	for (int i = 0; i < n; i++) {
		const ofRectangle& b = mask.blobs()[blobOrder[i]].boundingRect;

		// o crescimento do envelope acontece em torno do centro do contorno:
		// um blob que so cresce para a direita nao e o mesmo corpo, e uma
		// borda que corre contra a plateia
		const float cxn = space.maskToNormX(b.x + b.width * 0.5f);
		const float cyn = space.maskToNormY(b.y + b.height * 0.5f);
		const float hwn = space.maskToNormX(b.width * g * 0.5f);
		const float hhn = space.maskToNormY(b.height * g * 0.5f);

		CvBlobBox box;
		box.x0 = ofClamp(cxn - hwn, 0.0f, 1.0f);
		box.y0 = ofClamp(cyn - hhn, 0.0f, 1.0f);
		box.x1 = ofClamp(cxn + hwn, 0.0f, 1.0f);
		box.y1 = ofClamp(cyn + hhn, 0.0f, 1.0f);

		// identidade zero a pedido: e o registo que casa este centro com o do
		// frame anterior e decide se isto e um corpo que continua ou um que entrou
		blobs.push(box, ofRectangle(
			space.normToSrcX(cxn - hwn), space.normToSrcY(cyn - hhn),
			space.normToSrcX(hwn * 2.0f), space.normToSrcY(hhn * 2.0f)), 0);
	}
	blobs.endFrame();
	activeBlobs = blobs.count();
}

// =========================
void ofApp::pushSyntheticBlobs(float dt) {

	// o rato chega em coordenada de janela; o efeito vive em coordenada de fonte
	ofVec2f mouseSrc(space.winToSrcX(mouse.x), space.winToSrcY(mouse.y));

	blobs.beginFrame();
	int n = 0;

	for (size_t i = 0; i < rects.size() && n < CV_MAX_BLOBS; i++) {

		ofRectangle& r = rects[i];
		// a velocidade do idle tambem e um alvo do barramento: sem publico o
		// piece ainda respira, e e quanto ele respira que o ganho de velocidade
		// decide
		r.x += dir[i].x * (params.speed + rSpeed) * dt;
		r.y += dir[i].y * (params.speed + rSpeed) * dt;

		if (r.x < -r.width) r.x = space.srcW;
		else if (r.x > space.srcW) r.x = -r.width;
		if (r.y < -r.height) r.y = space.srcH;
		else if (r.y > space.srcH) r.y = -r.height;

		float cx = r.x + r.width * 0.5f;
		float cy = r.y + r.height * 0.5f;

		float dist = ofDist(cx, cy, mouseSrc.x, mouseSrc.y);
		float scale = ofMap(dist, 0, params.mouseRadius, params.scaleNear, params.scaleFar, true)
			* (1.0f + rGrow);

		float rw = r.width * scale;
		float rh = r.height * scale;
		float x0 = cx - rw * 0.5f;
		float y0 = cy - rh * 0.5f;

		CvBlobBox box;
		box.x0 = ofClamp(space.srcToNormX(x0), 0.0f, 1.0f);
		box.y0 = ofClamp(space.srcToNormY(y0), 0.0f, 1.0f);
		box.x1 = ofClamp(space.srcToNormX(x0 + rw), 0.0f, 1.0f);
		box.y1 = ofClamp(space.srcToNormY(y0 + rh), 0.0f, 1.0f);

		// aqui a identidade nao se procura, ja existe: o rect i e sempre o rect i
		// mesmo quando da a volta ao quadro e o centro salta de uma borda para a
		// outra, que e justamente o caso em que o casamento por centro mentiria
		blobs.push(box, ofRectangle(x0, y0, rw, rh), (int)i + 1);
		n++;
	}

	blobs.endFrame();
	activeBlobs = blobs.count();
}

// =========================
ofBaseDraws* ofApp::drawSource() {
	// o `&& texOk` que aqui estava nao sumiu: ele faz parte do fato que calcula
	// pixelsFromSource no update(), e escreve-lo duas vezes e o que permitia os
	// dois lados divergirem dentro do mesmo frame
	if (origin.pixelsFromSource) return static_cast<ofBaseDraws*>(&texLive);
	return static_cast<ofBaseDraws*>(&img);
}

// =========================
// A foto tem dois papeis e um dono so: e ela que aparece quando a fonte sai da
// tela e e no espaco dela que o baralho de retangulos e semeado. Desenha-la por
// uma funcao e semear o baralho por outra e a porta para o baralho nascer num
// espaco que a tela nao esta mostrando.
ofBaseDraws* ofApp::stillSource() {
	return static_cast<ofBaseDraws*>(&img);
}

// =========================
void ofApp::draw() {

	if (shaderOk && space.srcW > 0 && space.srcH > 0) {

		if (fbo.getWidth() != space.srcW || fbo.getHeight() != space.srcH) {
			fbo.allocate(space.srcW, space.srcH, GL_RGBA);
		}

		fx.begin();
		fx.setUniform1i("uEffect", effects.shaderId(params.effect));
		fx.setUniform1i("uBlobCount", activeBlobs);
		fx.setUniform1i("uFlipY", params.flipY ? 1 : 0);
		fx.setUniform1f("uThreshold", ofClamp(params.threshold + rThresh, 0.0f, 1.0f));
		fx.setUniform1f("uDitherBias", ofClamp(params.ditherBias + rDither, -1.0f, 1.0f));
		fx.setUniform1f("uExposure", rExposure);
		fx.setUniform4fv("uBlobs", blobs.upload(), CV_MAX_BLOBS);

		fbo.begin();
		ofClear(0, 255);
		if (params.showMask && origin.tracksSource && !mask.bin().empty()) {
			// a maquina sai menor que a fonte e o fbo tem o tamanho da fonte
			mask.gray().draw(0, 0, space.srcW, space.srcH);
		} else {
			drawSource()->draw(0, 0);
		}
		fbo.end();

		fx.end();
	}

	ofSetColor(255);
	if (fbo.isAllocated()) {
		fbo.draw(space.fit.x, space.fit.y, space.fit.width, space.fit.height);
	}

	if (params.showOverlay) drawOverlay();

	// O ultimo instante que ainda e nosso. O swap vem logo depois disto, dentro
	// do GLFW, e com vsync ligado e ele a decidir quando estes pixels chegam de
	// verdade ao monitor - por isso o `camara->tela` desta sonda e um intervalo
	// e nao um ponto.
	if (syncAtiva) syncProbe.drawFim(CvSyncProbe::agora());
}

// =========================
size_t ofApp::hudFmt(const char* fmt, ...) {

	// Um buffer so, sempre do mesmo sitio, e a linha desenhada logo a seguir.
	// _TRUNCATE e nao sprintf_s porque num buffer curto o sprintf_s chamaria o
	// handler de parametro invalido, que nesta build e derrubar o processo a
	// meio de um frame por causa de uma frase comprida.
	va_list ap;
	va_start(ap, fmt);
	const int w = _vsnprintf_s(hudTxt_, sizeof(hudTxt_), _TRUNCATE, fmt, ap);
	va_end(ap);
	if (w < 0 || w >= (int)sizeof(hudTxt_)) return strlen(hudTxt_);
	return (size_t)w;
}

// =========================
void ofApp::hudDraw(int& y, const char* prefix, const char* s, size_t n) {

	// assign e append dentro de uma capacidade reservada no setup: e assim que a
	// linha sai sem cadeia de operator+. A copia que o ofDrawBitmapString faz la
	// dentro e do openFrameworks, nao daqui, e nao ha como a evitar por esta API.
	hudLinha_.assign(prefix);
	hudLinha_.append(s, n);
	ofDrawBitmapString(hudLinha_, 10, y);
	y += 16;
}

// =========================
void ofApp::hudWrap(int& y, const char* prefix, const char* s, size_t n) {

	// a coluna esquerda tem 66 caracteres e o painel comeca no 67: o que passar
	// disso nao e cortado, passa por cima dos numeros. Quebra no ultimo espaco,
	// porque um numero partido ao meio nao se le.
	hudLinha_.assign(prefix);
	hudLinha_.append(s, n);
	size_t from = 0;
	while (from < hudLinha_.size()) {
		size_t len = hudLinha_.size() - from;
		if (len > 66) {
			const size_t sp = hudLinha_.find_last_of(" ", from + 66);
			len = (sp != string::npos && sp > from) ? (sp - from) : 66;
		}
		hudPeda_.assign(hudLinha_, from, len);
		ofDrawBitmapString(hudPeda_, 10, y);
		y += 16;
		from += len;
		while (from < hudLinha_.size() && hudLinha_[from] == ' ') from++;
	}
}

// =========================
void ofApp::drawOverlay() {

	ofNoFill();
	ofSetColor(255, 0, 0);

	float sx = space.fit.width / (float)MAX(1, space.srcW);
	float sy = space.fit.height / (float)MAX(1, space.srcH);

	// o retangulo do overlay e o mesmo slot do vec4 que foi para o shader, so
	// que em pixel de fonte: nao ha mais um scaled a parte para poder divergir
	for (int i = 0; i < blobs.count(); i++) {
		const ofRectangle& r = blobs.srcAt(i);
		ofDrawRectangle(
			space.fit.x + r.x * sx,
			space.fit.y + r.y * sy,
			r.width * sx,
			r.height * sy);
	}

	ofSetColor(255);

	// ---------- coluna esquerda: so a medicao ----------
	// O que um parametro vale e ate onde vai agora mora na tabela, a direita.
	// Aqui fica apenas o que se mede e nao se ajusta: taxa, blobs, tempos,
	// sinais e zonas. As duas colunas nao repetem numero nenhum.
	int y = 20;
	hudDraw(y, "", "Ctrl-v-0  |  medicao", strlen("Ctrl-v-0  |  medicao"));
	hudDraw(y, "", hudTxt_, hudFmt("%.1f fps  |  blobs %d", ofGetFrameRate(), activeBlobs));
	// "ligacoes" e o numero que prova a tecla repetida: se ela sobe a cada F2,
	// o palco esta pagando o cold start de novo; se fica, a tecla nao fez nada.
	hudDraw(y, "", hudTxt_,
		hudFmt("agora: %s  |  ligacoes %d", sources.name(params.source), kinectOpens));

	// o que o painel pergunta e de onde vem a maquina de contornos, nao que
	// pixels estao na tela: com o frame parado os tempos da camera continuam
	// sendo os tempos da camera, e a linha "sintetico (fallback)" so aparece
	// quando nao ha maquina nenhuma para medir
	if (origin.tracksSource) {
		hudWrap(y, "  ", capNote.data(), capNote.size());
		if (kinectOk) {
			// nada de ganho nem de limiar de movimento: aqui quem decide se e
			// corpo a distancia em milimetros, e ela esta na tabela da porta
			hudDraw(y, "", hudTxt_, hudFmt("  corpo %d%% do quadro  |  maquina %dx%d",
				mask.inside * 100 / MAX(1, space.maskW * space.maskH),
				space.maskW, space.maskH));
			hudWrap(y, "  ", kinect.mapNote.data(), kinect.mapNote.size());
			// statusNote e a linha mais larga do HUD: partida ao "|" ela cabe
			// na coluna em vez de passar por baixo do painel
			size_t from = 0;
			while (from < kinect.statusNote.size()) {
				const size_t next = kinect.statusNote.find("  |  ", from);
				if (next == string::npos) {
					hudDraw(y, "  ", kinect.statusNote.data() + from,
						kinect.statusNote.size() - from);
					break;
				}
				hudDraw(y, "  ", kinect.statusNote.data() + from, next - from);
				from = next + 5;
			}
		} else {
			hudDraw(y, "", hudTxt_, hudFmt("  mean %.1f/%.1f  |  gain %.2f  |  maquina %dx%d",
				mask.curMean, mask.bgMean, mask.gain, space.maskW, space.maskH));
		}
		hudDraw(y, "", hudTxt_, hudFmt("  espera camera %.1f ms  |  rastreio %.1f ms  |  ciclo %.1f ms",
			readMs, trackMs, cycleMs));
	} else {
		hudDraw(y, "", hudTxt_, hudFmt("  sintetico (fallback)  |  maquina %dx%d",
			space.maskW, space.maskH));
	}

	// O barramento na tela: alvo cru e envelope um ao lado do outro, porque em
	// cena a pergunta nunca e "quanto vale" e sim "por que o envelope atrasou
	// no que eu estou vendo".
	{
		hudDraw(y, "", hudTxt_, hudFmt(
			"  sinais  E %3.0f>%3.0f  D %3.0f  V %3.0f  C %3.0f  A %3.0f  |  %.2f ms",
			sig.energy * 100.0f, sig.energyE.v * 100.0f, sig.densityE.v * 100.0f,
			sig.speedE.v * 100.0f, sig.coherenceE.v * 100.0f, sig.ambientE.v * 100.0f, sigMs));

		// as zonas na ordem da grade, nao na ordem do array: quem olha para o
		// quadro precisa reconhecer o quadro. A linha e escrita de uma vez e
		// por isso e a grade do cvModel em letra: se a grade mudar, a
		// compilacao e que avisa, nao a tela com seis numeros a mais.
		static_assert(CV_ZONE_COLS == 3 && CV_ZONE_ROWS == 2,
			"a linha de zonas do HUD e a grade 3x2 escrita de uma vez");
		hudDraw(y, "", hudTxt_, hudFmt("  zonas  %3.0f %3.0f %3.0f |  %3.0f %3.0f %3.0f ",
			sig.zoneE[0].v * 100.0f, sig.zoneE[1].v * 100.0f, sig.zoneE[2].v * 100.0f,
			sig.zoneE[3].v * 100.0f, sig.zoneE[4].v * 100.0f, sig.zoneE[5].v * 100.0f));

		// o resultado do envelope vezes o ganho, nao o ganho: o ganho esta na
		// tabela a direita, esta linha e o que o render esta vendo agora. Os
		// rotulos sao curtos porque a linha partilha a coluna com o painel.
		hudDraw(y, "", hudTxt_, hudFmt(
			"  tela  exp +%.2f  grow +%.2f  umb +%.2f  dith +%.2f  vel +%.0f",
			rExposure, rGrow, rThresh, rDither, rSpeed));
	}

	// A sonda de transporte so existe com CV0_AUDIOPROBE no ambiente. Sem a
	// variavel esta linha nao nasce, e a coluna inteira fica byte a byte como
	// era antes do modulo de audio existir.
	if (audioProbe.ativo)
		hudWrap(y, "  audio: ", audioProbe.resumo.data(), audioProbe.resumo.size());
	if (midiProbe.ativo)
		hudWrap(y, "  midi: ", midiProbe.resumo.data(), midiProbe.resumo.size());

	// A saida de controle nao e sonda: ela existe em todo inicio, e por isso a
	// linha tambem existe em todo inicio - inclusive quando a porta nao foi
	// achada, que e justamente o caso em que o operador precisa de saber antes
	// de subir ao palco, sem abrir uma consola. O custo vai aqui pelo mesmo
	// motivo do sigMs: o que nao e medido em cena e o que se descobrira tarde.
	{
		const size_t n = midiOut.abriu()
			? hudFmt("  midi out: %s%s | cc %d/%d | pulsos %d | enviados %d recusa %d | %.2f ms",
				midiOut.nome().c_str(), midiOut.ligado() ? "" : " DESLIGADA (tecla o)",
				midiOut.ccVivos(), (int)CV_MIDI_CC, midiOut.pulsosVivos(),
				midiOut.enviados(), midiOut.recusados(), midiMs)
			: hudFmt("  midi out: %s", midiOut.estado().c_str());
		hudWrap(y, "", hudTxt_, n);
	}

	// O motor de sintese tambem nao e sonda: abre em todo inicio, e por isso a
	// linha tambem. O que ela mostra sao os unicos numeros que so a thread do
	// motorista sabe - se o callback veio, o pico do ultimo bloco, se o limiter
	// falou e quantos nascimentos nao couberam na fila. O `pub` e o custo do
	// outro lado, o da main: doze stores atomicos e a fila. Um motor mudo sem
	// esta linha e um app que parece estar bem e nao esta.
	{
		const size_t n = audioEngine.abriu()
			? hudFmt("  som: %s%s | pico %.4f | ag %.2f | vozes %d | ceif %ld | perd %ld | %.2f ms | pub %.3f ms",
				audioEngine.nome().c_str(), audioEngine.activo() ? "" : " MUDO (tecla u)",
				(double)audioEngine.pico(), (double)audioEngine.autoGain(),
				audioEngine.vozes(), audioEngine.ceifado(), audioEngine.pingPerdidos(),
				audioEngine.custoMs(), audioMs)
			: hudFmt("  som: %s", audioEngine.estado().c_str());
		hudWrap(y, "", hudTxt_, n);
	}

	// A sonda de churn obedece a CV0_CHURN: sem a variavel a linha nao existe e
	// a coluna fica o que era. Vai depois do cabo porque e do cabo que ela fala -
	// um nascimento e exatamente o que um pulso de nota e.
	if (churnAtiva)
		hudWrap(y, "  churn: ", churnNota, strlen(churnNota));

	// A sincronia obedece a CV0_SYNC e nao calcula nada aqui: a sonda escreve a
	// nota uma vez por janela e o HUD le o que ela deixou, pelo mesmo motivo
	// pelo qual o churn usa uma nota em vez de ordenar amostras no caminho do
	// pixel. Sem a variavel a linha nao existe e a coluna fica o que era.
	if (syncAtiva) {
		const char* sn = syncProbe.hudNota();
		hudWrap(y, "  sync: ", sn, strlen(sn));
	}

	// ---------- coluna direita: a tabela e os comandos ----------
	// O painel e desenhado a partir das mesmas linhas que as teclas usam. Ha
	// aqui uma so fonte de verdade, e o que ela mostra inclui o que nao tem
	// tecla: uma linha esmaea e um parametro que so se mexe recompilando.
	CvParamContext ctx;
	// as quatro linhas de texto do painel saem das tabelas: o backend e o modo
	// sao propriedades da linha da fonte, nao um if sobre um id solto no meio
	// do HUD. O que o painel mostra e o mesmo que a tecla usa.
	ctx.source = sources.name(params.source);
	ctx.backend = sources.backendText(params);
	ctx.mode = sources.modeText(params);
	ctx.effect = effects.name(params.effect);

	const int panelX = 540;
	int cy = table.drawPanel(params, ctx, panelX, 20) + 6;
	ofSetColor(150);
	ofDrawBitmapString("  COMANDOS", panelX, cy);
	ofSetColor(255);
	cy += 13;
	ofDrawBitmapString("  b refaz o fundo   r reseed   a novo rect", panelX, cy);
	cy += 13;
	ofDrawBitmapString("  p dialog ISO (so DSHOW)   s grava png", panelX, cy);
	cy += 13;
	ofDrawBitmapString("  o corta o cabo midi (a porta fica aberta)", panelX, cy);
	cy += 13;
	ofDrawBitmapString("  u corta o som (a rota fica aberta, pela rampa)", panelX, cy);

	// ---------- avisos: no pe da janela, a largura toda ----------
	// As strings de aviso sao as mais largas do HUD e nao cabem ao lado de
	// nada; em cena um aviso le-se uma vez, por isso ele pode descer. Aqui nao
	// ha vector nem string: os buffers sao do stack e um aviso mais que caiba
	// perde o proprio lugar, nao escreve fora de memoria. Oito lugares para os
	// sete que hoje existem, e um shader morto e o caso em que todos vao ao pe
	// ao mesmo tempo.
	{
		struct { ofColor c; char t[512]; } aviso[8];
		int nav = 0;

		auto set = [&](const ofColor& c, const char* s, size_t n) {
			if (nav >= 8) return;
			if (n >= sizeof(aviso[nav].t)) n = sizeof(aviso[nav].t) - 1;
			memcpy(aviso[nav].t, s, n);
			aviso[nav].t[n] = 0;
			aviso[nav].c = c;
			nav++;
		};

		if (!keyNote.empty()) set(ofColor(255, 220, 60), keyNote.data(), keyNote.size());
		if (kinectWaking) {
			// acordar nao e falha: se a linha vermelha significasse as duas coisas,
			// o operador desligaria um sensor que esta apenas ligando
			const size_t n = hudFmt("KINECT ACORDANDO %.0f s  |  %s",
				ofGetElapsedTimef() - kinectWakeStart, capNote.c_str());
			set(ofColor(255, 220, 60), hudTxt_, n);
		} else if (!capOk && !kinectOk) {
			const size_t n = hudFmt("FONTE NAO ABRIU: %s", capNote.c_str());
			set(ofColor(255, 60, 60), hudTxt_, n);
		}
		if (camStalled) {
			const size_t n = hudFmt("FRAME PARADO > %.1fs - watchdog assumiu, foto no lugar",
				(float)STALL_SECONDS);
			set(ofColor(255, 60, 60), hudTxt_, n);
		}
		if (!shaderOk) {
			set(ofColor(255, 60, 60), "SHADER NAO CARREGOU - veja a consola",
				strlen("SHADER NAO CARREGOU - veja a consola"));
		}
		// o contrato EffectTable<->frag: divergir significa que o painel anuncia
		// um efeito que o shader nao executa, e em cena isso e um botao que mente
		if (fxContract == 2) {
			const size_t n = hudFmt("EFEITO FORA DO FRAG: %s", fxContractNote.c_str());
			set(ofColor(255, 60, 60), hudTxt_, n);
		} else if (fxContract == 1) {
			set(ofColor(255, 220, 60), fxContractNote.data(), fxContractNote.size());
		}
		// o contrato do numero de slots, o mesmo tres estados: um frag que aloja
		// outros slots nao estraga um efeito, estraga silhuetas a mais
		if (blobContract == 2) {
			const size_t n = hudFmt("BLOBS FORA DO FRAG: %s", blobContractNote.c_str());
			set(ofColor(255, 60, 60), hudTxt_, n);
		} else if (blobContract == 1) {
			set(ofColor(255, 220, 60), blobContractNote.data(), blobContractNote.size());
		}
		if (!img.isAllocated()) {
			set(ofColor(255, 60, 60), "IMAGEM NAO CARREGOU - bin/data/0013.jpg",
				strlen("IMAGEM NAO CARREGOU - bin/data/0013.jpg"));
		}

		int ay = ofGetHeight() - 12 - nav * 16;
		for (int i = 0; i < nav; i++) {
			ofSetColor(aviso[i].c);
			hudDraw(ay, "", aviso[i].t, strlen(aviso[i].t));
		}

		ofSetColor(255);
	}

	ofSetColor(255);
}

// =========================
void ofApp::mouseMoved(int x, int y) {
	mouse.set(x, y);
}

// =========================
void ofApp::keyPressed(int key) {

	keyNote.clear();

	// o efeito e a linha da tabela que declara esta tecla. O inteiro que vai
	// para o shader e um dado da linha, nao a sua posicao, por isso inserir um
	// efeito novo no meio deixa de empurrar os outros em silencio.
	const CvEffectRow* er = effects.byKey(key);
	if (er) params.effect = (CvEffect)er->id;

	// a ligacao do Kinect nao aceita interrupcao: trocar de fonte no meio dos
	// 25 s deixaria um sensor aberto sem dono do outro lado da thread
	const bool trocaFonte = sources.byKey(key) != nullptr
		|| key == 'c' || key == 'v';
	if (trocaFonte && kinectWaking) {
		keyNote = "Kinect acordando (" + ofToString(ofGetElapsedTimef() - kinectWakeStart, 0)
			+ " s): aguarde a ligacao acabar";
		return;
	}

	// F1..F4 saem da coluna de teclas da tabela de fontes, na ordem que ele
	// declarou; uma fonte nova e uma linha la e chega aqui sozinha
	const CvSourceRow* sr = sources.byKey(key);
	if (sr) { params.source = (CvSource)sr->id; openSource(); }

	if (key == 'b') { mask.invalidateBackground(); mask.seedBackground(); }
	if (key == 't') params.showMask = !params.showMask;

	// o dialog do proprio driver: e aqui que a ISO segura a luz do palco. Ele
	// so existe no DSHOW; no MSMF a tecla avisa em vez de nao fazer nada.
	if (key == 'p') {
		if (!capOk) keyNote = "p: nenhuma fonte aberta";
		else if (capApi == cv::CAP_DSHOW) cap.set(cv::CAP_PROP_SETTINGS, 1);
		else keyNote = "p: o dialog de ISO so abre no DSHOW - tecla c troca o backend";
	}

	// o outro backend: DSHOW para mexer na camera, MSMF para os 30 fps. Aqui o
	// pedido muda de verdade, entao a ligacao e forcada mesmo com a fonte viva.
	// Uma fonte que nao passa pelo cv::VideoCapture nao tem backend a trocar, e
	// a nota dela vem na linha da tabela em vez de um if sobre o id.
	if (key == 'c') {
		const char* note = sources.backendNote(params.source);
		if (note) {
			keyNote = note;
		} else {
			params.dshowFirst = !params.dshowFirst;
			openSource(true);
		}
	}

	// A porta de distancia tem duas bordas e cada uma tem o seu par: , . e a
	// PERTO, que corta o chao e a mesa em frente ao sensor, e [ ] e a LONGE,
	// que corta o fundo da sala. O teto e 4000 porque alem disso o 1473 nao
	// reporta: o longe demais chega como zero e zero ja fica fora da porta.
	// A faixa e o passo de cada uma vivem na tabela, nao aqui: esta linha diz
	// apenas qual borda a tecla empurra e em que direcao.
	if (key == ',') table.nudge(params, P_NEAR, -1.0f);
	if (key == '.') table.nudge(params, P_NEAR, 1.0f);

	// na webcam os mesmos [ ] seguem no limiar de movimento: la nao ha porta,
	// e a tabela diz em que linha cada um dos dois esta vivo em cada momento
	if (key == '[' || key == ']') {
		const float dir = (key == '[') ? -1.0f : 1.0f;
		if (kinectOk) table.nudge(params, P_FAR, dir);
		else table.nudge(params, P_MOTION, dir);
	}

	// a resolucao do rastreio e a alavanca do fps: se a luz do palco exigir o
	// frame inteiro, sobe; se o processamento atrasar, desce
	if (key == 'z') table.nudge(params, P_TRACKW, -1.0f);
	if (key == 'x') table.nudge(params, P_TRACKW, 1.0f);

	// A escada 640x480 / 1280x720 existe numa linha so, na SourceTable: o que o
	// HUD le como "modo pedido" e o que esta tecla escreve sao os dois lados do
	// mesmo numero, e era precisamente isso que a tabela de parametros veio
	// acabar. No 1473 nao ha modo a escolher: os dois streams sao 640x480 por
	// definicao, e reabrir aqui custaria o cold start por nada.
	if (key == 'v') {
		if (sources.cycleMode(params)) {
			openSource(true);
		} else {
			const char* note = sources.modeNote(params.source);
			if (note) keyNote = note;
		}
	}

	if (key == 'a') {
		addRect(
			ofRandom(space.srcW > 0 ? space.srcW : 640),
			ofRandom(space.srcH > 0 ? space.srcH : 480),
			ofRandom(40, 120),
			ofRandom(40, 120));
	}
	if (key == 'r') reseed();
	if (key == 'd') params.showOverlay = !params.showOverlay;
	if (key == 'f') params.flipY = !params.flipY;

	// o master em uma tecla: desligar a reacao no meio de uma cena nao pode
	// exigir zerar cinco ganhos, e os ganhos continuam todos acesos por baixo
	if (key == 'g') {
		params.reactive = !params.reactive;
		keyNote = params.reactive
			? "g: reativo LIGADO"
			: "g: reativo DESLIGADO - o render volta ao que era antes do pacote 4";
	}
	if (key == 'n' || key == 'm') {
		const float dir = (key == 'n') ? -1.0f : 1.0f;
		const float v = table.nudge(params, P_MASTER, dir);
		keyNote = "master " + ofToString(v, 2);
	}

	// uma tecla, dois alvos: o corte e o grao do preto-e-branco andam juntos
	// porque em cena ninguem sobe um deles sozinho
	if (key == '+' || key == '=') {
		table.nudge(params, P_THRESH, 1.0f);
		table.nudge(params, P_DITHER, 1.0f);
	}
	if (key == '-' || key == '_') {
		table.nudge(params, P_THRESH, -1.0f);
		table.nudge(params, P_DITHER, -1.0f);
	}

	if (key == 's') {
		ofSaveScreen("cv0_" + ofToString(ofGetFrameNum()) + ".png");
	}

	// O cabo desliga-se sem se fechar: a porta continua aberta e os valores que
	// la do outro lado ja estao ficam onde estao, que e o contrario de mandar
	// zeros. Cortar o MIDI no meio de uma cena nao pode obrigar a reabrir nada.
	if (key == 'o') {
		midiOut.toggle();
		keyNote = midiOut.ligado()
			? "o: saida midi LIGADA"
			: "o: saida midi DESLIGADA - a porta fica aberta, o cabo nao fala";
	}

	// O som corta-se pela rampa, nunca pelo ganho: um zero posto a meio de um
	// periodo e um clique na sala, e num palco um clique ouve-se mais alto do
	// que a nota que ele cortou. A rota fica aberta e o motorista continua a
	// vir - e a rampa de 120 ms que faz o silencio.
	if (key == 'u') {
		audioEngine.toggle();
		keyNote = audioEngine.activo()
			? "u: som LIGADO (rampa de 0,12 s)"
			: "u: som MUDO (rampa de 0,12 s) - a rota fica aberta";
	}
}

// =========================
void ofApp::windowResized(int w, int h) {
	space.updateFit(w, h);
}

// =========================
void ofApp::reseed() {
	rects.clear();
	dir.clear();
	// os rectangulos novos sao outros corpos: um id do baralho anterior nao pode
	// ser reencontrado por um indice que agora aponta para outra coisa
	blobs.reset();
	// o baralho e semeado no espaco do fallback desenhado, nao no da foto por
	// nome: quando o rastreio cai nos rectangulos, o que esta na tela e
	// stillSource(), e o espaco que o push sintetico usa para normalizar e o do
	// que foi desenhado. Amarrar os dois a uma funcao so e o que impede a
	// divergencia de acontecer em silencio
	for (int i = 0; i < 20; i++) {
		addRect(
			ofRandom(stillSource()->getWidth()),
			ofRandom(stillSource()->getHeight()),
			ofRandom(40, 120),
			ofRandom(40, 120));
	}
}

// =========================
void ofApp::addRect(float x, float y, float w, float h) {

	if (w <= 0) w = 1;
	if (h <= 0) h = 1;

	rects.push_back(ofRectangle(x, y, w, h));

	float angle = ofRandom(TWO_PI);
	dir.push_back(ofVec2f(cos(angle), sin(angle)));
}


