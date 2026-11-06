#pragma once

#include <string>
#include <vector>

// ==================== Sonda de transporte de audio (pacote 5, passo 1) ====================
// Este modulo so existe para responder uma pergunta que hoje esta no escuro:
// nesta maquina, com o AudioSrv em loop de quebra, QUAL transporte de audio
// abre de verdade e a que custo. Nada aqui e motor de audio: nao ha sintese,
// nao ha ganho, o buffer devolvido e silencio puro.
//
// A porta de entrada e a variavel de ambiente CV0_AUDIOPROBE. Sem ela,
// audioProbeAtiva() devolve false e nenhuma linha abaixo e executada; o app
// continua exatamente o que era antes deste ficheiro existir.
//
// Opcao de fuga: CV0_AUDIOPROBE_API vale "asio", "wasapi", "ds" ou "all"
// (default). Ela existe porque uma rota pode derrubar o processo antes das
// outras serem medidas, e nesse caso a leitura continua em outro binario com
// uma unica API.

struct AudioRoute {
	std::string api;          // "asio" / "wasapi" / "ds" / "dummy"
	std::string pedidos;      // API pedida, para registrar substituicao
	bool pediu = false;       // a rota entrou na varredura nesta execucao
	bool compilado = false;   // a lib tem este backend dentro
	int dispositivos = 0;     // quantos sairam da enumeracao
	std::string nomes;        // nomes dos dispositivos, " | "
	std::string abriu;        // dispositivo que aceitou o openStream
	std::string motivo;       // por que nenhuma rota abriu, ou o ultimo erro
	int tentativas = 0;       // combinacoes taxaxbufferxformato tentadas
	long taxa = 0;            // Hz realmente aceito
	int buffer = 0;           // quadros por callback realmente usados
	int buffers = 0;          // numero de buffers internos do transporte
	std::string formato;      // "float32" / "int16"
	int canais = 0;
	long callbacks = 0;       // quantas vezes o driver chamou o nosso codigo
	double parede = 0.0;      // segundos de tempo real da janela de medicao
	double relogio = 0.0;     // segundos que o relogio do proprio stream avancou
	long underflow = 0;       // callbacks que chegaram com flag de underflow
	long relogio_paro = 0;    // callbacks com streamTime identico ao anterior
	double latencia_ms = 0.0; // latencia interna reportada pelo transporte
	double ms_callback = 0.0; // media de tempo gasto dentro do callback
};

struct AudioProbeReport {
	bool ativo = false;
	std::string versao;        // RtAudio::getVersion()
	std::string compilados;    // backends presentes na lib
	std::vector<AudioRoute> rotas;
	bool algumaRou = false;    // pelo menos uma rota com callbacks > 0
	std::string resumo;        // uma linha curta para o HUD
};

// true se CV0_AUDIOPROBE existe e nao esta vazia
bool audioProbeAtiva();

// Executa a varredura. Bloqueia a main thread por ate ~1,5 s por rota viva e
// imprime cada resultado em stderr assim que ele existe, antes de ir para a
// proxima API: se uma rota derrubar o processo, as anteriores ja foram lidas.
void audioProbeRun(AudioProbeReport& rep);
