#pragma once

#include <string>
#include <vector>

// ==================== Sonda de MIDI (pacote 5, rota de saida) ====================
// Responde a pergunta que a enumeracao nao responde: uma mensagem enviada pela
// ponta OUT de um cabo volta pela ponta IN dele? Enumerar porta e barato e nao
// prova entrega; o laco fechado prova.
//
// A porta de entrada e CV0_MIDIPROBE. Sem ela a funcao le a variavel, ve que
// nao existe e volta sem fazer nada; nem a linha "midi:" do HUD nasce.
// Opcional: CV0_MIDIPROBE_PAR com uma substring do nome limita o laco a um
// cabo so.
//
// Nada aqui instala, escreve registro ou toca servico. Abre e fecha portas
// pela winmm, que ja esta linkada pelo openFrameworksRelease.props.

struct MidiPort {
	int indice = -1;
	std::string nome;
	unsigned int tecnologia = 0;	// wTechnology da saida
	unsigned int suporte = 0;		// dwSupport
	bool capsOk = false;			// midiXxxGetDevCapsW devolveu 0
	unsigned int retornoCaps = 0;	// codigo cru quando nao devolveu 0
};

struct MidiLoop {
	std::string nome;			// cabo pareado pelo nome nas duas listas
	int idxOut = -1;
	int idxIn = -1;
	bool abriuOut = false;
	bool abriuIn = false;
	unsigned int erroOut = 0;
	unsigned int erroIn = 0;
	int enviados = 0;
	int recebidos = 0;
	int casados = 0;			// recebidos iguais a algo que foi enviado
	int outros = 0;			// mensagens que nao sao MIM_DATA
	double ms_ate_primeira = -1.0;	// -1 = nada chegou
	double ms_janela = 0.0;
	std::string nota;
};

struct MidiProbeReport {
	bool ativo = false;
	std::vector<MidiPort> saidas;
	std::vector<MidiPort> entradas;
	std::vector<MidiLoop> lacos;
	int lacosVivos = 0;
	std::string resumo;
};

bool midiProbeAtiva();
void midiProbeRun(MidiProbeReport& rep);
