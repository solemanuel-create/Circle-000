# Dependencias do Ctrl-v-0

Registro unico do que este projeto linka, de onde cada coisa vem e do que uma
dependencia nova exige da maquina. Escrito a partir do `ctrlv0.vcxproj` (ate
2026-11-06 de manha este projeto chamava-se `polygonExample`; ver §9), das
`.props` do openFrameworks e de medicao nesta maquina em 2026-11-06. Se uma
linha aqui contradisser o arquivo, o arquivo manda: cada referencia abaixo e de
linha do proprio projeto.

---

## 0. A regra dura, provada hoje: a ESCRITA de log derruba o processo

O openFrameworks nao pode escrever uma linha de log nesta build. Nao e o call que
mata, e o write: com `OF_LOG_SILENT` (o nivel que `src/main.cpp:11` impoe) toda
mensagem e filtrada antes de sair, e por isso o app sobrevive ao ofxOpenCv, que
loga ao alocar. Basta subir o nivel para a primeira escrita matar.

Medicao de hoje, com a sonda atras de `CV0_LOGPROBE=1` (`ofApp::logProbe`):

```
[probe] nivel antes da sonda = 5 (0 = SILENT)
[probe] A1 antes de ofLogError com construtor de duas palavras
        (fim do arquivo - o processo morreu aqui)
```

Morreu na **primeira** escrita, feita pelo nosso proprio codigo, antes de
qualquer addon, antes de qualquer camera. Codigo de saida `c0000005`
(access violation) dentro do `polygonExample.exe` (o nome do artefato ate
2026-11-06; hoje e `ctrlv0.exe`), registrado no Application
Log do Windows (evento 1001, modulo `polygonExample.exe`, excecao
`0xc0000005`). Os marcadores seguintes (B: `operator<<`, C: stdout, D: addon,
E: core) nao foram alcancados.

Refeito as 10:22 no `bin\polygonExample.exe` embarcado, nao numa saida
alternativa: mesma saida, mesma morte no A1. A sonda nao e artefato de build
temporaria, e o comportamento do binario de palco. **A morte e do write, nao do
nome do arquivo: o `bin\ctrlv0.exe` de hoje herda a regra e `OF_LOG_SILENT`
continua estrutural.**

Consequencia pratica, e a razao deste arquivo existir:

- **Antes de adicionar qualquer addon, isto tem que estar resolvido.** Addon que
  loga em nivel nao-silente mata o app na primeira mensagem, e a morte acontece
  dentro do codigo do addon, nao no nosso.
- Nada de contar com `ofLog` para diagnosticar dependencia nova. O canal que
  funciona e `cv0Log` (stderr sem buffer, `ofApp.cpp:40`), e as notas que o HUD
  mostra.
- O `OF_LOG_SILENT` de `main.cpp:11` **nao e burocracia**: e o que segura o app
  de pe. Trocar por qualquer nivel visivel e pedir o crash.
- Causa-raiz ainda nao apurada. O proximo passo e dentro de
  `ofConsoleLoggerChannel::log` (`libs/openFrameworks/utils/ofLog.cpp:233-243`),
  que escreve em `std::cout`/`std::cerr`, ou na lista de canais que o `ofLogBase`
  mantem. Uma sessao de debug com o offset do crash no exe responde isso em
  minutos; por linha de codigo nao se responde.

Nota de honestidade: a premissa ja tinha sido provada em sessoes anteriores e o
core corrompido de 2026-06-14 foi restaurado (`quarantine-core-2026-06-14/`
guarda os `ofLog.cpp` e `ofImage.cpp` truncados; os atuais foram gravados em
05-11 as 14:55 e o `openframeworksLib.lib` reconstruido as 15:58). A sonda de
hoje refaz a prova **depois** dessa restauracao: o crash continua. A premissa nao
estava expirada.

---

## 1. O que este projeto linka

| o que | vem de | declarado em | exige da maquina |
|---|---|---|---|
| openFrameworks 0.12.1 | `libs/openFrameworksCompiled/lib/vs/x64/openframeworksLib.lib` | via `ProjectReference` (`vcxproj:298`) e as `.props` importadas em `vcxproj:59` e `:63` | o rebuild do core (lento) se os fontes dele mudarem |
| RtAudio 6.0.1 (o `ofSoundStream` do OF) | `libs/rtAudio/lib/vs/x64/rtaudio.lib` (`rtaudioD.lib` em Debug) + `libs/rtAudio/include/RtAudio.h` | **sem entrada propria no `.vcxproj`**: vem dos caminhos de include e lib das `.props` do OF (`openFrameworksRelease.props:13` e `:18-19`), importadas em `vcxproj:59` | nada alem do core ja compilado. **A versao e a API nova** (`RtAudio::Api`, `RtAudio::StreamOptions`, contagem de dispositivos por `getDeviceCount`/`DeviceInfo`): quem escrever contra a API antiga nao recebe um erro de versao, recebe `C2039` em nomes que simplesmente nao existem. O `RTAUDIO_VERSION` esta chumbado em `RtAudio.h:49-51` |
| ofxGui | `addons/ofxGui/src/*.cpp` compilados por glob | glob em `vcxproj:257-260`; include em `vcxproj:234` | nada alem do proprio OF |
| ofxOpenCv (OpenCV 4.11) | glob em `vcxproj:261-264` + libs nomeadas | libs em `vcxproj:240` (Debug) e `:253` (Release); include em `:234` | os `.dll` do OpenCV ao lado do exe ou no PATH |
| Kinect SDK v1.8 (NuiApi) | `Kinect10.lib` (import lib do `Kinect10.dll` do System32) | libs em `vcxproj:240` e `:253`; include `$(KINECTSDK10_DIR)inc` em `:234` | **SDK v1.8 instalado** e a variavel de ambiente de maquina `KINECTSDK10_DIR` |
| DirectShow / Media Foundation | `#pragma comment(lib, ...)` dentro de `src/cvDevices.cpp:8-12` | o proprio arquivo cpp | nada, sao do Windows |
| `ole32/uuid/mfplat/mf/mfuuid` | idem | `cvDevices.cpp:8-12` | idem |

Nada de `ofxNDI`, `ofxEdsdk` ou `ofxImGui` esta no projeto, e **nada disso e
preciso** para as fontes hoje: EOS e NDI chegam como camera virtual pelo
DirectShow/MSMF (a enumeracao real desta maquina em 06-11:
`[dshow] 0 "EOS Webcam Utility"`, `[dshow] 7 "EOS Webcam Utility Pro"`,
`[dshow] 2..5 "NDI Webcam Video 1..4"`, `[msmf] 2..5` idem).

## 2. Matriz de configuracoes - onde o projeto esta incompleto

O `.vcxproj` declara seis configuracoes (`vcxproj:4-24`): Debug/Release em x64,
ARM64 e ARM64EC. As dependencias de addon so existem em x64:

| bloco | condicao | o que da |
|---|---|---|
| includes de addon (`:228-236`) | `'$(Platform)'=='x64'` | ofxGui, ofxOpenCv, OpenCV, NuiApi |
| libs Debug (`:237-242`) | `'Debug|x64'` | opencv `...d` + Kinect10 |
| libs Release (`:243-255`) | `'Release|x64'` | opencv + Kinect10 |

Ou seja: **ARM64 e ARM64EC nao herdam include nem lib de addon**. Compilam ate
`ofApp.h` e morrem em `#include "ofxOpenCv.h"`. Isto nao e um problema de hoje
(a maquina e x64), mas e a primeira coisa que alguém tropeca ao clicar em ARM64.
Ou se completa os tres blocos, ou se tiram as configuracoes ARM do `.sln` para o
erro virar impossibilidade declarada. Decisao pendente.

## 3. Variaveis de ambiente que o build le

| variavel | definida por | se faltar |
|---|---|---|
| `OF_ROOT` | **nao e ambiente**: e propriedade definida em `libs/openFrameworksCompiled/project/vs/openFrameworksCommon.props:5` como caminho relativo ao arquivo | o import das `.props` falha e nada compila |
| `KINECTSDK10_DIR` | instalador do Kinect SDK v1.8, variavel de **maquina**; ja visto apontar para `C:\Program Files\Microsoft SDKs\Kinect\v1.8\` **com barra no fim** (por isso os caminhos no projeto sao `$(KINECTSDK10_DIR)inc`, sem barra) | `NuiApi.h` nao resolve e `Kinect10.lib` nao linka; a mensagem nao diz que a causa e a variavel |

## 4. A ordem das libs e ela propria um remendo

`vcxproj:246-253` explica, mas aqui e onde alguem vai procurar antes de mexer:
o `opencv_world` traz uma segunda copia do `cap_dshow.obj`, que e a classe
`videoInput` do openFrameworks. As libs do addon vao **depois** de
`%(AdditionalDependencies)` para que o `videoInput.lib` do OF ganhe a disputa.
Inverter essa ordem faz o `ofVideoGrabber` chamar uma copia cujo layout de objeto
e outro, e o app morre em pontos aleatorios. Nao e supersticao, e o motivo pelo
qual o `Ctrl-v-0` nem usa `ofVideoGrabber` (o `videoInput` dele segfaulta ao
listar dispositivos nesta instalacao; a entrada passou por `cv::VideoCapture`).

## 5. Como adicionar um addon, nesta instalacao, sem ProjectGenerator

Nao ha ProjectGenerator aqui, entao `addons.make` (que lista `ofxGui` e
`ofxOpenCv`) **e decorativo**: nada no build o le. Os passos reais, todos em
`ctrlv0.vcxproj`:

1. include: acrescentar `$(OF_ROOT)\addons\<nome>\src` a `:234`.
2. glob de fontes: acrescentar o par `<ClCompile Include="$(OF_ROOT)\addons\<nome>\src\*.cpp">`
   com `<ObjectFileName>$(IntDir)Build\<nome>\%(Filename).obj</ObjectFileName>` e o
   `ClInclude` correspondente, junto de `:257-264`. O glob quer o
   `ObjectFileName` separado: sem ele dois addons com arquivo de mesmo nome
   colidem no `.obj`.
3. libs: acrescentar as `.lib` em `:240` **e** `:253`, e os diretorios em `:239`
   e `:245`.
4. runtime: os `.dll` do addon vao em `bin\` (a pasta `dll\x64` do projeto esta
   **vazia** e o passo pos-build que copiaria de la so imprime
   `Arquivo nao encontrado - *.dll / 0 arquivos copiados`; nao e erro, e o
   projeto nao tem DLLs proprios).
5. registrar aqui, linha nova na tabela da secao 1, com o que ele exige da
   maquina - e, agora, se ele loga.

## 6. Como adicionar um arquivo proprio em `src/`

Tambem a mao, em tres lugares: `<ClCompile Include="src\arquivo.cpp" />` no grupo
de `:267-279`, `<ClInclude .../>` no grupo de `:282-295` e, para o VS nao mostrar
tudo solto, o mesmo arquivo em `ctrlv0.vcxproj.filters`. Os tres grupos
estao completos desde o P5: treze `.cpp` (audioEngine, audioProbe, blobModel,
cvDevices, effectTable, kinect1473, main, maskPipeline, midiOut, midiProbe,
ofApp, paramTable, sourceTable) e catorze headers
(audioEngine, audioProbe, blobModel, cvDevices, cvModel, cvSpace, effectTable,
kinect1473, maskPipeline, midiOut, midiProbe, ofApp, paramTable, sourceTable).
O `filters` nao e redundante: `audioEngine.h` ficou de fora dele na primeira
versao do modulo, e o `.vcxproj` compilou e linkou igual — o unico sintoma foi o
VS mostrar o header fora do grupo `src`. A queixa antiga - "o filters so lista
ofApp.cpp, main.cpp e ofApp.h" - nao vale mais. O que vale e o risco: um arquivo novo no
`src/` sem as tres entradas nao aparece no VS e, se for `.cpp`, nao e compilado,
o que da `LNK2019` em vez de um erro de fonte.

## 7. Como compilar sem brigar com uma instancia viva

A trava e da pasta de onde o exe foi lancado, nao do projeto: uma instancia
aberta trava o `ctrlv0.exe` **daquela pasta** e o link contra esse
mesmo arquivo devolve `LNK1104`. Medido em 2026-11-06, quando o artefato ainda
se chamava `polygonExample.exe`: duas instancias vivas
lançadas de saidas alternativas deixavam `bin\polygonExample.exe` totalmente
livre, e a build normal de `Release|x64` linkou no `bin` sem fechar nada.
Antes de pedir para fechar uma instancia, faca o teste de abertura exclusiva no
alvo - um `FileStream(..., ReadWrite, None)` que passa significa que o link
passa.

A saida alternativa em si e uma build normal com a pasta trocada:

```
MSBuild.exe ctrlv0.vcxproj -p:Configuration=Release -p:Platform=x64 ^
  -p:OutDir=C:\caminho\temp\build\ -p:BuildProjectReferences=false
```

`BuildProjectReferences=false` evita reconstruir o core (132 MB de `.lib`,
minutos). Uma saida fora do `bin` precisa de duas coisas ao lado do exe: a copia
de `bin\data` (o caminho de dados e `<pasta do exe>\data`, ver
`libs/openFrameworks/utils/ofFileUtils.cpp:29-45`) e o
`openframeworksLib.lib` na propria saida, porque o link pede
`$(OutDir)openframeworksLib.lib`.

## 8. Como rodar a sonda

```
CV0_LOGPROBE=1 CV0_SRC=kinect ctrlv0.exe > out.txt 2> err.txt
```

`CV0_SRC` escolhe a fonte no `setup()` (`ofApp.cpp:162-178`) e e a unica forma
de provar uma fonte sem mao no teclado - nao ha injecao de tecla nesta
plataforma, entao o que depende de tecla continua provando por variavel de
ambiente e por captura de janela. Sem `CV0_LOGPROBE` a funcao retorna na primeira
linha e nao escreve nada: medido hoje, a saida de um run sem a variavel e
byte a byte a mesma estrutura do run anterior, sem nenhuma linha `[probe]`.

Outras variaveis de runtime, todas com a mesma regra de inercia:

| variavel | o que faz | onde |
|---|---|---|
| `CV0_AUDIOPROBE` | sonda de transporte de audio, uma vez no `setup()` | `src/audioProbe.cpp` |
| `CV0_MIDIPROBE` | sonda de laco MIDI objetivo (OUT e IN do mesmo cabo); `CV0_MIDIPROBE_PAR` limita ao cabo | `src/midiProbe.cpp` |
| `CV0_MIDIOUT` | **agente, nao sonda**: escolhe a saida MIDI por nome; `off` ou `0` desliga o modulo | `src/midiOut.cpp` |
| `CV0_CHURN` | sonda de procedencia dos nascimentos do registo: de 5 em 5 s separa `vazio` / `longe` / `ocupado` / `forcado`, com os baldes de distancia do candidato mais proximo, e o ledger do gatilho (`vive`/`morre` a 8 frames, `sozinho`/`perto-velho`/`perto-novo`, a `curva` de sobrevivencia a 1/2/3/4/8/16 frames, `desc`) | `src/blobModel.cpp`, `ofApp::churnDump()` |
| `CV0_BLOBMATCH` | limiar de casamento de ids em distancia normalizada (padrao `0.10`, cortado a `0.02..0.60`); sem a variavel o valor e o de sempre | `src/ofApp.cpp` (`setup`), `BlobRegistry::match_` |
| `CV0_BLOBAREA` | corte de area minima (`minAreaPct`) para a varredura da fragmentacao; cortado a `0.001..0.05`, que e a faixa da linha do painel, e imprime o valor de palco que substituiu | `src/ofApp.cpp` (`setup`), `CvParams::minAreaPct` |
| `CV0_MIDIAGE` | **agente, nao sonda**: idade em frames a que um corpo da a nota, cortado a `0..60`; `0` repoe exatamente o gatilho antigo e e por isso que existe, para o A/B ter um lado velho. Padrao compilado `3` | `src/midiOut.cpp` (`setup`), `CV_MIDI_PULSO_IDADE` |
| `CV0_AUDIOEMIT` | **teste, nao sonda de transporte**: o valor e a duracao em segundos (cortada a `1..30`; texto que nao e numero cai em 5 e diz isso na linha de abertura). Poe na rota que abrir um sinus de 440 Hz a -18 dBFS, constantes chumbadas no fonte | `src/audioProbe.cpp` (`audioEmitRun`), chamado em `ofApp::setup()` |
| `CV0_AUDIOEMIT_PANEL` | **segunda variavel do mesmo teste, nao um modo**: qualquer valor excepto `0`/`off` pede o painel do proprio ASIO4ALL, e so na rota ASIO. Sem ela o comportamento e o que foi medido antes | `src/audioProbe.cpp` (`audioEmitRun` → `emitRota`) |
| `CV0_AUDIOENG` | **agente, nao sonda**: `off` ou `0` desliga o motor de sintetese por inteiro — nenhuma rota aberta, nenhuma amostra escrita, e o HUD diz `som: desligada por CV0_AUDIOENG` em vez de mentir com um `pico 0.0000`. Sem a variavel o motor abre com o exe, exactamente como o `midiOut` | `src/audioEngine.cpp` (`AudioEngine::setup`), chamado em `ofApp.cpp:137` |
| `CV0_AUDIOENG_NIVEL` | teto da sala em **dBFS**, cortado a `-40..-6`; fora dessa faixa ou texto que nao e numero, fica o teto de palco (`-18`) e a linha de abertura diz qual ficou e porquê. O valor e a unica entrada do limiter, nao um ganho master | `src/audioEngine.cpp` (`setup`, antes da rota) |
| `CV0_AUDIOENG_TEST` | **prova por fora, nao um modo de palco**: o valor e a duracao em segundos por cena (`1..4`, texto nao numerico cai em 1). Corre a mesma funcao que o motorista usa, sem dispositivo, e imprime a tabela das cenas mais o WAV do conjunto. Roda **antes** de qualquer tentativa de abrir rota, para os numeros sobreviverem a uma rota que derrube o processo | `src/audioEngine.cpp` (`setup` → `testeOffline`) |
| `CV0_SYNC` | sonda de sincronia ponta a ponta: carimba o instante de cada frame de camara, do fim da publicacao, de cada callback do motorista, do nascimento de cada corpo (por id, nao por slot) e de cada nota, e de 5 em 5 s imprime seis canais com `n(+delta) fora min/med/p90/max` mais os compostos `cam->tela`, `cam->som`, `nasce->nota`. Nao muda um numero do comportamento | `src/syncProbe.{h,cpp}`, `ofApp::syncDump()` |
| `CV0_SYNC_VSYNC` | **segunda variavel da mesma sonda**: `off`/`0`/`n`/`false` poe `ofSetVerticalSync(false)`, qualquer outra coisa ou ausencia poe `true`. So existe quando `CV0_SYNC` esta ligado, porque sem sonda armadas nao ha porque tocar na janela de quem nao pediu a medicao | `src/ofApp.cpp` (`setup`, bloco da sonda) |
| `CV0_AUDIOENG_WAV` | despejo da saida **viva**: o valor e a duracao em segundos (`1..30`, cortado, e default 5). Reserva um buffer de `s x 48000 x 2` float numa unica alocacao no `setup()` — zero alocacao no caminho do motorista — e escreve `cv0_audio_eng_vivo.wav` na pasta do exe (PCM16, o que o motorista entregou, nao o que o motor sonhou) | `src/audioEngine.cpp` (`setup`, `Ctx::dump`) |

O `CV0_BLOBMATCH` e o `CV0_BLOBAREA` nao sao sondas: mudam o comportamento. So
aparecem em uma tabela porque a medicao das duas alavancas so existe junto com a
sonda do churn, que passou a imprimir o limiar vigente no primeiro campo da
linha — e o `CV0_BLOBAREA` imprime na primeira linha o valor de palco que ele
substituiu, porque uma varredura que nao diz de onde partiu nao e uma varredura.
A decisao deliberada de deixar `match_` de fora do `reset()`: uma corrida de tres
limiares nao pode perder a condicao que esta a medir a meio, quando alguem
carrega em `r`.

A do churn e o caso em que a medicao nao muda o caminho medido: sem a variavel
`churnAtivo_` e falso, `push()` so faz um teste de bool a mais, nenhum campo de
`CvChurnProbe` e escrito e a linha do HUD nao existe. O `CV0_MIDIOUT` esta na
lista porque obedece a uma variavel mas nao e sonda - abre em todo inicio.

O `CV0_MIDIAGE` e o outro lado dessa distincao: tambem nao e sonda, tambem abre
com o exe, e a diferenca e que ele mudou o que a saida faz. O numero compilado
(`3`) veio de uma corrida de 30 s no Kinect com o limiar a `0.15` e o corte de
palco, lida pela boca do `cv0_midi_listen.exe`, que e outro programa:

| gatilho | notas/s no cabo | custo em latencia |
|---|---|---|
| idade `0` (nascimento) | 21,2 | 0 ms, e o churn todo a soar |
| idade `3` | 5,5 | ~100 ms aos 30,6 fps medidos no mesmo run |

Os 120 ms da peca sao o teto, e a 32,7 ms por frame o degrau quatro ja nao cabia
nele; a curva imprimiu `1:49% 2:30% 3:21% 4:16% 8:6% 16:2%` sobre 547 nascimentos
reais, ou seja, o debounce nao esta a suprimir corpos, esta a suprimir fragmentos
que duravam menos de um terco de segundo. A invariante do ledger fecha em todas
as janelas (`desc 0`, `morre + saidos + pendentes = nascidos - forcados`).

O `CV0_AUDIOEMIT` existe porque a varredura de transporte devolveu silencio, e
silencio nao pode ser ouvido: a pergunta que sobrou nao era "que rota abre" e sim
"sai alguma coisa da caixa", e essa so uma pessoa na sala responde. Medido numa
janela de 5 s, com o exe de um OutDir alternativo:

```
EMISSAO: asio | ASIO4ALL v2 | 48000 Hz | float32 | 2 canais | buffer 512 quadros
EMISSAO: 5 s de 440 Hz a -18.0 dBFS = amplitude 0.1258 (contagem de float32)
EMISSAO: callbacks 463 em 5.00 s | relogio do stream 4.93 s | amostras escritas
         237056 (= 4.93 s x taxa) | pico escrito 0.1258
```

Tres coisas dessa leitura. O sinus nasce de um **contador de amostras**, nao de um
acumulador de fase em ponto flutuante: somar um incremento por amostra a 48 kHz
perde o ultimo digito e o que se ouve entao e o batimento do acumulo, nao o
transporte — com o contador a frequencia e exata por construcao. O `pico escrito`
e `0.1258`, que e `10^(-18/20)` ao centesimo: a medicao e feita sobre a mesma
conta que escreve no buffer, portanto nao ha ganho escondido entre o numero e o
que foi entregue ao motorista. E o `relogio do stream` andou `4.93 s` em `5.00 s`
de parede, com `237056 = 463 x 512` — o transporte consumiu em tempo real, nao
ficou a engolir o mesmo bloco.

O teste tem as duas acusacoes proprias (`relogio < 0.2 x parede` e `amostras <
0.5 x parede x taxa`) porque um "pico escrito" alto sobre um stream que nao anda
e um numero bonito sobre silencio; nenhuma disparou. O controle negativo tambem
foi feito: sem a variavel, 8 s de app vivo e **zero linhas `[audio]`**.

O que nada disso prova e o que importa. O pico e nosso, medido no buffer que nos
escrevemos; a rota WASAPI/DS nem foi tentada porque o laco para na primeira que
abre, e a saida fisica do ASIO4ALL e um ajuste do painel dele que este codigo nao
le. A unica linha que fecha o pacote 5 nao sai do stderr: e ele a dizer se ouviu.

O painel respondeu a essa ultima parte, e por isso o `CV0_AUDIOEMIT_PANEL` existe:
o mapeamento WDM do ASIO4ALL **so existe em texto dentro daquela janela**. No
registo (`HKCU:\SOFTWARE\ASIO4ALL v2 by Wuschel`) os dispositivos sao chaves
hash (`5EDDBCF5`, `9BBAE383`) com `BufferSize 512`, `KernelBuffers 2` e
`LtcyIn/LtcyOut 32` — estrutura legivel, nomes nao. Aberto pelo proprio app, o
painel mostra **Realtek(R) Audio** como endpoint activo (com NVIDIA High
Definition Audio, Kinect USB Audio e NDI Webcam Audio na lista, desligados) e
`Tamanho da memoria intermediaria ASIO: 512`. E a mesma conta que a emissao usou,
e o mesmo codec cuja cadeia de APOs e a que quebra o `Audiosrv`: o ASIO4ALL
nao toca no servico, fala com o pin WDM/kernel do Realtek por cima dele.
O `ASIOControlPanel` devolve `0` em `0,000 s` — nao bloqueia — e a medicao com o
painel aberto deu `743 callbacks em 8.00 s | relogio 7.92 s | 380416 amostras |
pico 0.1258`, ou seja, a janela do driver nao rouba tempo ao transporte.

### O motor de sintetese (`CV0_AUDIOENG*`)

O motor le o `CvSignals` (a mesma fonte do `midiOut`) e escreve no buffer do
motorista. Nada mais. As quatro decisoes que estao no `audioEngine.h` e que nao
se veem do HUD:

| sinal | o que faz no som | por que esse numero |
|---|---|---|
| `coerencia` | escolha do colchao: quinta justa (ratio `1.5`) quando a sala esta amarrada, tritono (`sqrt(2)`) quando esta espalhada | **escolha, nao medida** |
| `zone[i]` | nota `57 + GRAUS[col] + 12*(ROWS-1-row)`; linha 0 = topo da imagem = oitava de cima | zona 0 = nota 69 = A4 = 440 Hz, a referencia que se pode ouvir |
| `densidade` | passador-baixo `fc = 180 * 33^densidade` | escolha |
| `velocidade` | tremolo do ar, `1.5 + 11*velocidade` Hz | escolha |
| idade do corpo | pulso: nota curta quando um id completa `CV_MIDI_PULSO_IDADE` frames | **medida** — e o mesmo N da curva de sobrevivencia |
| teto | `-18 dBFS` e **o clip point**, nao o full-scale | **medido** como o pico que ele ouvia sem distorcao |

Duas regras de conta que este passo estabeleceu e que valem para qualquer
medicao de audio da arvore, porque as duas primeiras analises por fora mentiram:

- **Delta por canal e a frames vizinhos.** O metrico `salto` de `testeOffline`
  comparava o canal esquerdo contra o direito do mesmo instante, o que e uma
  conta de largura esterica vestida de deteccao de clique: o mesmo salto deu
  `0,03576` por dentro e `0,00276` por fora. Corrigido para `anterior[2]`.
- **O bloco de asentamento nao e a cena.** O passador-baixo tem memoria; os
  primeiros `512` quadros de cada cena pertencem ao som da anterior. As
  colunas `pico`/`rms`/`fora do teto` so contam depois dele (`b > 0` em C++,
  `ASS = 512` no script de analise por fora, para as duas linguagens lerem a
  mesma janela).

A tabela abaixo e a leitura final, com o exe de um OutDir alternativo e
`CV0_AUDIOENG_TEST=1` (1 s por cena, 48 kHz, 2 canais):

```
sala vazia      pico 0.0000 (-99.0) | rms 0.0000 | fora do teto 0 | salto 0.00000
colchao quinta  pico 0.0557 (-25.1) | rms 0.0289 | salto 0.00092
colchao tritono pico 0.0564 (-25.0) | rms 0.0290 | salto 0.00094
zona 0 so       pico 0.0095 (-40.5) | rms 0.0043 | salto 0.00060
as seis zonas   pico 0.0906 (-20.9) | rms 0.0348 | salto 0.00335
densidade 0     pico 0.0861 (-21.3) | rms 0.0328 | salto 0.00309
densidade 1     pico 0.1259 (-18.0) | rms 0.0445 | fora do teto 48 | salto 0.01217
so ar           pico 0.0012 (-58.3) | rms 0.0002 | crest ~6 (ruido, como declarado)
pulsos          pico 0.0142 (-37.0) | rms 0.0038 | salto 0.00091
rampa de mute   pico 0.0900 (-20.9) | salto 0.00312
  rampa | maior salto da 2a metade 0.00222 em t=0.5120 s (corte em 0.5000)
        | na fronteira de bloco 0.00180 | cauda depois de 0.641 s 0.00000 | ga no fim 0.9998
master 2        pico 0.1259 (-18.0) | rms 0.0483 | fora do teto 525 | salto 0.02743
gravado: 571392 quadros | pico do ficheiro 0.1258 | ceifados na copia 0
```

As tres colunas da rampa existem porque a rampa tem tres doencas diferentes e o
`salto` sozinho nao as separa: o `quando` acusa o corte, a `fronteira de bloco`
acusa o degrau do limiter e a `cauda` acusa o motor que nao se cala. Aqui as tres
estao limpas, e a segunda metade do mute faz saltos **menores** do que os do
mesmo som a tocar sem mute — que e o que tem de ser para o `u` nao se ouvir.

A analise por fora (`cv0_audioeng_wav.py`, no Temp) concena cena com esta tabela e
acrescenta o que os numeros sozinhos nao dizem: o espectro prova o colchao
(110 + 165 com 155,56 **ausente**; 110 + 155,56 com 165 **ausente**), prova a
zona 0 em 440, prova as seis zonas vivas, e conta **zero amostras acima do teto
em 1 142 784**. O ficheiro tem `2*canais` bytes por quadro — ler como se fossem
`2` divide cada cena ao meio e duplica-a.

No `bin` de palco, sem nenhuma variavel, a saida do motor e:

```
som: ASIO4ALL v2 | pico 0.0104 | ag 1.00 | vozes 8 | ceif 0 | perd 0 | 0.15 ms | pub 0.001 ms
```

com `31,3 fps / ciclo 22,2 ms`, `466-473 callbacks em 5 s`, `underflow 0` e custo
por callback `0,11-0,17 ms` (orcamento: 10,7 ms por bloco). `u` muda para
`som: ASIO4ALL v2 MUDO (tecla u) | pico 0.0000`, e o motorista mede `pico 0.0000
(silencio dBFS)` — a rota fica aberta, o que se cala e o motor. O despejo de 8 s
da sala real deu `pico L 0.02917 | pico R 0.03116 | rms mono 0.00456`, com conteudo
em 261,63 / 523,25 / 110 Hz e zero acima do teto.

O `pub` e o custo do lado da main (doze stores atomicos e a fila da entrada), e
ele existe no HUD pela mesma razao do `midiMs`: o que nao e medido em cena e o
que se descobrira tarde. Medido a 30,6 fps: `0,001 ms`. A linha passou a caber em
duas voltas da coluna de 66 caracteres, provado por screenshot, nao por conta.

O controle negativo ao vivo tambem foi feito, e ele nao e o `CV0_AUDIOENG=off`:
e a **sala vazia a rota aberta**. Numa corrida sem ninguem em frente a camera,
com o barramento vivo (`midi out: enviados 132`, os onze CC a modular), o motor
deu `pico 0.0000 | vozes 0` e o motorista `callbacks 468 em 5.03 s | relogio
4.99 s | amostras 239616 | dentro do callback 0.097 ms`. Ou seja: o silencio que
se ouviu nao foi rota morta nem transporte parado — foi a ausencia de corpo, que
e exatamente a cena 1 da tabela offline.

**O que o motor ainda nao garante.** Uma segunda instancia do mesmo exe morre a
`0xc0000005` em `ntdll.dll` dentro do `audioEngine.setup()`, antes de imprimir
qualquer linha `[audio-eng]` — reproduzido duas vezes. O ASIO4ALL entrega o
dispositivo a um processo de cada vez, e aqui a recusa do segundo nao vem como
erro: vem como acesso violado. Isto contradiz a promessa escrita em
`ofApp.cpp:131-136` ("uma segunda instancia tem de dizer 'nenhuma rota abriu' e
continuar a existir"). O remendo proposto — mutex nomeado global tomado **antes**
de tocar na API ASIO; se ja esta tomado, dizer `rota ASIO ja tomada por outra
instancia`, cair para WASAPI/DS e continuar vivo — **nao esta implementado**:
ninguem o autorizou. Nota separada, e nao e codigo: duas instancias vivas fazem o
MSMF devolver `-1072873854` na camera (secao 9).

O preco pago por essa descoberta e pratico: cada morte deixa um re-lancamento do
WER sem janela a viver, e enquanto eles vivem o `bin\ctrlv0.exe` esta **travado**
(o teste de `FileStream(..., ReadWrite, None)` devolve "usado por outro
processo"). Desde 2026-11-06 que os PIDs `30320` e `19688` seguram o artefacto de
palco, e o que nao e deles: nao abrem o ASIO (uma instancia nova, lancada depois
deles, abriu a rota e entregou 468 callbacks em 5 s), nao ocupam a camera e nao
aparecem na lista de janelas. Nada foi fechado por decisao: matar processo alheio
nao e do agente. Enquanto viverem, o bin de palco nao pode ser reescrito e as
provas correm na saida alternativa.

Duas armadilhas de ferramenta que este passo pagou, e que valem para qualquer
medicao futura nesta maquina:

- **`reg.exe` a partir do Git Bash nao le nada aqui.** As primeiras respostas
  "(ausente)" para `HKLM\SOFTWARE\ASIO` eram falsas: o mesmo `reg query` falha
  ate para chaves que tem de existir. A leitura de registo e so em PowerShell,
  e com uma chave de controle impressa na mesma saida.
- **O print de ecra precisa de duas contas antes do recorte.** A tela virtual
  desta maquina comeca em `x = -1280`, entao uma janela em `6,6` cai em `1286`
  no png; e sem `SetProcessDPIAware()` o `GetWindowRect` devolve pixeis fisicos
  enquanto o `CopyFromScreen` devolve virtuais. A primeira captura do painel
  apanhou o Qoder por cima, nao o painel.

Duas regras que nasceram de sondas que mentiram sem querer, e valem para
qualquer sonda de igualdade desta arvore:

- **O orcamento tem que ser contado por rota, nao em frames totais.** Um run com
  o Kinect frio fez 10077 frames e a sonda teria dito "0 falhas" sobre eles,
  sendo que 0 frames eram da rota da camera: o cold start roda a ~144 fps e
  gasta qualquer teto de frames antes de o sensor acordar. A sonda do P5 passou a
  contar `p5Ncam` e a se acusar (`VACUO: a rota fonte deu 0 de 300 frames`). O
  teto do run tem que ser em segundos.
- **Zero divergencias so nao e prova.** Toda sonda precisa de um controle
  negativo semeado - uma formula deliberadamente errada que a tabela tem que
  acusar. No P5 a formula sem o `&& texOk` acusou 24 de 128 linhas; sem essa
  linha o "0 divergencias" poderia significar simplesmente que a comparacao nao
  rodou.

### A sonda de sincronia (`CV0_SYNC`)

Medida em 2026-11-07 no `bin` de palco, em tres passes de ~38 s, com a webcam
integrada em `MSMF #1` a 1280x720 (995 frames de fonte em ~35 s = 28,4/s) e o
motor ASIO4ALL aberto a 48k/512/2 canais. Cada passe acabou pela tecla Escape do
proprio openFrameworks (`ofMainLoop.cpp:265`, `escapeQuits` por omissao): nenhum
processo foi morto para fechar uma corrida.

| canal | vsync ON | vsync OFF | o que e |
|---|---|---|---|
| `cam->tela` med / p90 / max | 2,5 / 3,4 / 25,5 | 2,2 / 3,1 / 25,3 | do `read()` do frame ate ao fim do `draw` |
| `swap` min / med / p90 / max | 1,2 / 6,2 / 65,2 / 82,7 | 1,2 / 6,4 / 65,2 / 81,8 | intervalo entre draws; a mediana e o arranque sem camera |
| `cam->pub` med | 1,9 | 1,7 | do `read()` ate a publicacao dos centros |
| `pub->bloco` med / p90 / max | 29,5 / 55,8 / 90,1 | 31,0 / 57,2 / 81,0 | da publicacao ate ao callback que a le |
| COMPOSTO `cam->som` | 31,4 | 32,7 | `cam->pub` + `pub->bloco` |

Tres coisas que so a medicao podia dizer:

1. **O vsync nao e a alavanca, e o `swap min` e que o prova.** Com
   `glfwSwapInterval(1)` *pedido*, o menor intervalo entre draws medido foi
   **1,2 ms** -- nenhum ecran a 60 Hz, e sequer a 144, deixa passar um swap a
   1,2 ms. ON e OFF coincidem a 0,2 ms na mediana e exatamente a 65,2 ms no p90.
   A OF so expoe o setter (`ofAppRunner.h:107` -> `ofAppGLFWWindow.cpp:1662`) e
   nao ha leitura de volta, por isso a linha da sonda escreve o **pedido** e a
   distribuicao do `swap` e a prova de que ele nao bateu. Ficar com o vsync ON
   nao custa nada medido; nao contar com ele e o que custa.
2. **A entrega da camera vem em rajadas.** 995 draws em ~35 s dao 35 ms de media,
   mas a distribuicao e bimodal: metade dos intervalos ~6 ms, 10% acima de 65 ms,
   teto 82,7. Nao e o loop que flutua -- e o `MSMF` a soltar dois frames de uma
   vez e depois a esperar. Para a reactividade isto e o numero a atacar, porque
   e aqui que a imagem salta.
3. **O maior numero do caminho do som nao esta no sintetizador, e nao era a
   espera da nota.** `pub->bloco` da mediana ~30 ms e cauda 90 ms, e ate ao
   passo 62 chamava-se-lhe o tempo que a nota espera no anel. O que ele mede e
   a idade dos ESCALARES - energia, brilho, ganho - no instante em que cada
   bloco os le. A espera de uma nota concreta passou a oitavo canal e deu
   **6,7 ms** com teto de um bloco. Ver "O oitavo canal", abaixo. O motor em si
   custa 0,076 ms dentro do callback e o `underflow` foi sempre 0.

Limites declarados pela propria sonda: `cam->tela` comeca no `read()`, portanto a
idade do frame dentro do driver e do USB antes de o ler **nao entra** no numero,
e e justamente a fatia que falta ao orcamento de 120 ms; e `nasce->nota` so tem
valor quando ha gente em cena. A primeira destas duas cotas tem casa propria
desde o passo 61, no sub-capitulo seguinte; a segunda continua a pertencer a um
ensaio.

Do `nasce->nota`: na corrida de 35 s com que o passo 60 foi fechado o canal nao
deu uma amostra valida em nenhum dos lados, porque nenhum corpo chegou aos 3
frames de vida -- a sala estava vazia. Na passagem de 14 s com a mesma fonte e
movimento em cena deu 18 nascimentos: `min 69,6 med 75,8 p90 116,7 max 201,9`. E
na corrida do passo 62, com gente a mexer-se em frente a webcam, deu 35 notas
com o gatilho e o transporte medidos um por um (tabela acima). Continua a ser o
unico dos tres compostos que precisa de corpo, e por isso o ensaio com uma pessoa
a sério (passo 63) ainda esta em dívida: o que falta nao e o canal ter amostras,
e saber quantas delas existem com um dançarino a mover-se de caso pensado.

O controlo negativo fechou a conta do custo: sem `CV0_SYNC` nao ha **uma** linha
`[sync]` nos 30 de stderr, os callbacks do motor sao os mesmos tres nos tres
passes (3680 / 3686 / 3687 em ~35 s, `ceifados 0`, `underflow 0`) e a saida da
sonda custa 0,02 a 0,07 ms por janela de 5 s.

Receita do harness, porque a camera e o recurso que trava primeiro: lancar
corridas seguidas sem descanso faz o `MSMF` recusar o streaming
(`OnReadSample() error -1072873854`) e o app cai para `DSHOW #1`, que nesta
maquina entrega ~0,8 fps -- a 1 frame por segundo nenhum corpo chega a idade 3,
o `nasce->nota` fica vazio e os restantes canais medem uma coisa que nao e a
peca. Ja tinha sido medido em 2026-11-05 e passa sozinho em ~10 min. O
`cv0_p60_run.ps1` (em `%TEMP%`) po por isso uma porta de saude: so mede quando a
linha `[fonte]` diz `MSMF`, descansa 45 s entre tentativas e espera 9 min antes
da primeira. O `cv0_p62_run.ps1` e o `cv0_p62b_run.ps1` herdaram a mesma porta
sem espera inicial, porque a camera ja tinha descansado entre as corridas, e o
`cv0_p62_offline.ps1` corre a tabela com a fonte NDI para nao tocar de todo na
integrada. Todos os scripts do passo, o backup do `bin` anterior e os stderr de
cada passe ficaram em `%TEMP%` com o prefixo `cv0_p62`.

### O setimo canal (`driver->idade`) e o carimbo que nao existe

O passo 61 foi pedido como "medir a idade do frame dentro do driver por
`cv::CAP_PROP_FRAME_TIMESTAMP`". Essa propriedade **nao existe nesta build** e
foi preciso espreitar o header para o saber, porque nada a pede em voz alta: o
enum `VideoCaptureProperties` do OpenCV 4.11 que o ofxOpenCv traz
(`addons/ofxOpenCv/libs/opencv/include/opencv2/videoio.hpp`, linhas 140-215)
vai de 0 a 72 e nao tem nenhuma marca de tempo por frame. As duas candidatas
que la estao declaram no comentario ser do backend FFmpeg -- `CAP_PROP_PTS` (71)
e `CAP_PROP_STREAM_OPEN_TIME_USEC` (55) -- e FFmpeg nao esta nesta maquina. O
perigo real nao e o `cap.get()` falhar: uma propriedade que nao existe nao
devolve um erro, devolve o numero da propriedade que ocupa aquele indice, e 30
e `ISO_SPEED`, 29 e `GUID`. Um "0" lido de la seria interpretado como "o frame
e fresco".

O que ficou no lugar e uma cota deduzida do ritmo de entrega, e vai escrita como
cota:

- cada `cap.read()` e carimbado por dentro (`ofApp::update`, duas chamadas de
  `steady_clock` so quando `CV0_SYNC` existe);
- espera >= 2 ms = a fila estava **vazia** no inicio da chamada, portanto o
  frame que ela trouxe foi produzido depois desse inicio e a sua idade e no
  maximo o tempo que a chamada esperou;
- espera < 2 ms = o frame ja la estava, e como nada na fila pode ter sido
  produzido antes do inicio do ciclo, a idade e no maximo `agora - inicio do
  ciclo`;
- `rajada` conta as leituras instantaneas consecutivas, que e a assinatura de
  agrupamento do backend em inteiro, e nao em milisegundos.

Medido numa passe de ~35 s com a integrada em `MSMF #1` (991 frames de fonte,
`fora 0` em todos os canais):

| quantidade | valor |
|---|---|
| `driver->idade` min / med / p90 / max | 10,7 / 58,7 / 72,9 / 214,6 ms |
| leituras que esperaram / instantaneas | 493 / 498 de 991 |
| rajadas: comprimento 1 / 2 / 5 | 419 / 37 / 1 |
| `cam->tela` (o que a peca faz) med | 2,7 ms |
| **`camera real->tela` (limite)** | **<= 61,4 .. 83,4 ms** |

Tres coisas que valiam a corrida:

1. **A fatia que faltava ao orcamento de 120 ms era a maior de todas.** Os seis
   canais de passo 60 somavam ~9,4 ms entre o `read()` e o fim do draw; o que o
   pixel ja tinha vivido antes de chegar la e ate 59 ms de mediana e 73 ms de
   p90. O orcamento fecha-se por baixo: mesmo a pior leitura medida (idade p90 +
   `cam->tela` p90) sao 83,4 ms, com ~37 ms de folga, e a saida fisica do
   ASIO4ALL (buffer 512 = 10,7 ms) ainda nem entrou nela porque nao e legivel
   sem perguntar ao driver.
2. **A bimodalidade do `swap` tem agora nome e tamanho.** Metade das leituras
   espera e metade apanha a fila cheia, e 37 rajadas de 2 + uma de 5 dao o
   formato do agrupamento: o backend entrega aos pares, e por isso os 6 ms de
   metade dos draws e os 65 ms da outra metade. Nao e o loop a flutuar, e a
   fila, e a idade que ela paga e a que a tabela acima mede.
3. **O limite e um teto, nao uma estimativa, e diz-se.** Numa entrega aos pares,
   o frame mais velho do par tinha de estar vivo a meia periodo, nao aos 59 ms
   inteiros: o que a ausencia do carimbo nao permite e separar as duas coisas
   por mediem. Para fechar isso ha duas rotas e nenhuma delas e um numero do
   fonte: ler o `IMFSample` pelo Media Foundation a mao (um caminho de captura
   novo por cima do `cv::VideoCapture`) ou filmar a propria projecao com a
   segunda camera e medir o atraso entre o frame desenhado e o frame visto, que
   e o unico numero que inclui ate a exposicao do sensor.

O controlo negativo voltou a fechar a conta do custo: sem `CV0_SYNC` a passe nao
emite **uma** linha `[sync]`, e os callbacks do motor batem certo com a corrida
armada janela a janela (470 e 469 nos ultimos 5,00 s nas duas; relogio acumulado
35,02 s nas duas; `dentro do callback` 0,086 contra 0,087 ms). Com a sonda
armada a saida passou a custar 0,046 a 0,147 ms por janela, porque sao sete
canais a ordenar em vez de seis.

Armadilha de leitura deixada a descoberto por esta corrida, e remendada no
passo 62: quando `nasce->nota` nao tem amostras, a composta escrevia `med 0.0`
e o `transporte` saia negativo (`0 - 3xswap` = `-23,4`). O AVISO de "nenhuma
nota saiu pelo motorista" estava na mesma linha, mas um numero negativo a pedir
ao leitor que o some nao e uma ausencia dita em voz alta. Hoje a decomposicao
toda sai como `n/a` quando nao ha notas drenadas.

### O oitavo canal (`pub->nota`) e os dois numeros que partilhavam um nome

O passo 62 foi autorizado como "cortar os ~30 ms de `pub->bloco`". Antes de
cortar seja o que foi foi-se ver o que esse canal mede, e a resposta nao e a que
o nome prometie:

| canal | o que escreve | o que responde | onde bate |
|---|---|---|---|
| `pub->bloco` (passo 60) | TODOS os callbacks, contra a ULTIMA publicacao | com que idade estao os doze escalares quando o bloco os le | o ritmo a que a main publica, que e o ritmo dos frames da camera |
| `pub->nota` (passo 62) | so os Eventos que sairam do anel, cada um com o seu carimbo | quanto esperou ESTA nota na fila | um bloco (10,7 ms) mais o agendamento da thread do motorista |

Nao e o mesmo numero por uma razao de estrutura: o anel dos eventos e drenado
todo em cada callback, mas os atomicos do envelope sao lidos seja qual for o
ultimo valor que passou pela main. Uma sala sem gente enche `pub->bloco` na
mesma e deixa `pub->nota` sem uma amostra; uma fonte agarrada leva o primeiro ao
teto da escala enquanto o segundo continua vazio. Foi exactamente isso que a
primeira corrida deste passo mostrou, sem ser procurado: com a fonte NDI
virtual da maquina (`NDI Webcam Video 1 | MSMF #2`, que entrega 3 frames em 30 s
e depois da `can't grab frame. Error: -2147483638` a cada 10 s), o `pub->bloco`
deu mediana **3263 ms** com `fora 1294` e o `pub->nota` deu `n=0`. Os dois
canais nao podem ser a mesma coisa nenhuma.

Como foi feito: o `Evento` passou a levar `pubUs`, posto no mesmo
`CvSyncProbe::agora()` que carimba o nascimento, e o motorista subtrai-lhe o
instante de entrada no callback. Sem sonda armada os dois carimbos ficam zero,
o `Evento` e byte a byte o que sempre foi nos tres campos que o sintetizador le,
e a prova disso e a tabela offline: as 13 linhas `TESTE` sairam **identicas** ao
`cv0_audioeng_teste.err` de 2026-11-06 -- `571392 quadros`, pico do ficheiro
`0.1258`, `ceifados na copia 0`, e nenhuma das medianas, saltos ou `fora do
teto` das 12 cenas mudou um digito. O que mudou na diff foi so o caminho do WAV.

Medido numa corrida de webcam de 35 s, MSMF #1 a 30 fps, com movimento em frente
a camera e 35 notas a sair pelo motorista:

| troco | min | med | p90 | max |
|---|---|---|---|---|
| nasce->pub = gatilho (3 frames de vida) | 62,2 | 68,4 | 84,8 | 133,2 |
| pub->nota = transporte (espera no anel) | 1,0 | **6,7** | 8,7 | 10,0 |
| nasce->nota = ponta a ponta | 64,4 | 75,9 | 92,3 | 140,5 |

68,4 + 6,7 = 75,1 contra 75,9 medidos de ponta a ponta: os ~0,8 ms de diferenca
sao o enfileirar em si, e a conta fecha sem nenhuma pata inventada. Foi assim
que se viu que a decomposicao do passo 60 estava errada para cima por 25 ms: ela
estimava o gatilho como `3 x swap`, e uma corrida com cena da swap com mediana
31,4 ms (sao as rajadas do backend), logo `3x31,4 = 94,2` para um gatilho que
mede 68,4 -- e o resto, chamado transporte, saia `-18,3`. A linha agora escreve
os dois medidos e mantem a estimativa antiga ao lado, com o nome de estimativa.

**O que sobra para cortar, e a quem pertence decidir.** Refeita a conta, os 30
ms nao eram espera de nota, e nao ha 30 ms a cortar no transporte: ele ja vale
6,7 ms com teto de um bloco. Ficam tres alavancas, e as tres tem preco:

1. `buffer 512 -> 256` no ASIO4ALL corta metade da espera do anel (~3,4 ms) e
   estreita sobretudo a cota que o driver poe depois do callback, que e a parte
   do `cam->som` ainda nao medida e declarada desde o passo 60. Preco: margem de
   xrun numa Realtek partilhada, e em palco um xrun ouve-se. Decisao dele.
2. Os 68,4 ms de gatilho sao os tres frames de vida que a curva de sobrevivencia
   escolheu (N=8 custava 267 ms de atraso e N=1 devolvia os nascimentos falsos
   que o debounce veio matar). Baixar para N=2 corta ~23 ms e volta a aceitar
   mais corpos que se fragmentaram. Decisao dele.
3. A idade dos escalares (~30 ms, cauda 90) so se corta se o bloco deixar de
   ler o ultimo valor publicado a saltos e passar a interpolar ate ele -- que e
   mexer no som, nao na fiao. Decisao dele, e estetica.

O custo da propria saida subiu com o oitavo canal: 0,020 a 0,144 ms por janela
(de 8 canais a ordenar em vez de 7), e o controle negativo manteve-se limpo --
sem `CV0_SYNC` a passe nao emite uma linha `[sync]` e os callbacks do motor
batem certo com a corrida armada (938 nos ultimos 10 s nas duas, `dentro do
callback` 0,077 ms nas duas, `underflow 0`).

## 9. Armadilhas ja pagas, listadas para quem chegar depois

- Titulo da janela e `Ctrl-v-0 TRACK`; a captura por substring pega o Visual
  Studio e o Explorador de Arquivos junto. Medido hoje, com tres instancias do
  app abertas ao mesmo tempo, o substring nao decide **qual** das tres ele pega:
  a captura passou a ser pelo PID que o proprio script abriu (`MainWindowHandle`
  do processo), e so assim o print e prova do run que se fez.
- Duas instancias vivas da integrada, ou uma morta a `taskkill` ha pouco, fazem o
  MSMF devolver `-1072873854` e o DSHOW entregar frame parado; a maquina se
  recupera sozinha em minutos. Nao e codigo.
- `SDK v1.8 viu 0 sensores` aparece com o Kinect Service frio. Medido hoje de
  manha: dois runs seguidos com 0 sensores, um com duas tentativas. A tentativa
  dupla nao e bug: rodando por script, sem ninguem no teclado, saiu **uma**
  tentativa so; as duas anteriores foram tecla fisica.
- Os shaders de `bin/data/shadersGL3` so existem no renderer programavel:
  `main.cpp` pede `setGLVersion(3,3)`. Com 2.x o `fx.load` falha e o HUD acusa.
- O artefato chamava-se `polygonExample` porque nasceu do exemplo do
  openFrameworks. **Renomeado para `ctrlv0` em 2026-11-06**: `git mv` de
  `ctrlv0.vcxproj` e `ctrlv0.vcxproj.filters`, entrada do `Ctrl-v-0.sln` (o GUID
  `{7FD42DF7-...}` ficou o mesmo de proposito, mudar ele nao muda nada e quebraria
  o estado guardado no `.vs`), `<RootNamespace>`, `Abrir-Ctrl-v-0.bat` e o README
  da raiz, que agora e o da peca. O `ctrlv0.vcxproj.user` **nao e rastreado**
  (esta no `.gitignore`), entao ele nao segue o `git mv` e foi movido a mao. Os
  artefatos antigos - `bin\polygonExample*.exe/.lib/.exp/.pdb` e a papelada de
  `obj\` - estao em `quarantine-nomes-2026-11-06\`, nao apagados. O exe novo nao
  muda caminho de dados: `bin\data\` e lido relativo ao exe, e o contrato do frag
  (`[efeitos] 3 efeitos` / `[blobs] 32 slots`) continua saindo no stderr do
  `ctrlv0.exe`.
- O `polygonExample.png` na raiz era a captura do exemplo, referenciada so pelo
  README antigo; ficou em `quarantine-nomes-2026-11-06\`.
- A trava de link e da **pasta de onde o exe foi lancado**, nao do projeto:
  instancia rodando de uma saida alternativa deixa o `bin` livre. Antes de
  fechar qualquer coisa, teste a abertura exclusiva no alvo.
- Uma **segunda instancia** do `ctrlv0.exe` morre a `0xc0000005` em `ntdll.dll`
  dentro de `audioEngine.setup()`, sem imprimir nenhuma linha `[audio-eng]`: o
  ASIO4ALL da o dispositivo a um processo de cada vez e aqui a recusa nao vem como
  erro, vem como acesso violado. Reproduzido duas vezes em 2026-11-06, **ainda nao
  corrigido** (o remendo proposto esta na secao 8). Cada morte deixa um re-lançamento
  do WER sem janela a viver em background. Nao e a camera e nao e o `ofSoundStream`:
  uma instancia so, lancada depois, abre ASIO e camera normalmente ao lado deles.
- Fora do projeto, mas no caminho do pacote de audio: `audiosrv.dll`, o servico
  de audio do Windows, quebrou 67 vezes em dois dias com `0xc0000005`, em ciclo
  de poucos minutos (contado no Application Log em 2026-11-06). Som em cena
  depende desse servico, nao so do nosso `ofSoundStream`.
- `cv::CAP_PROP_FRAME_TIMESTAMP` **nao existe** no OpenCV 4.11 do ofxOpenCv
  (enum em `videoio.hpp`: 0 a 72, sem marca de tempo por frame; `CAP_PROP_PTS`
  e `CAP_PROP_STREAM_OPEN_TIME_USEC` sao FFmpeg-only e FFmpeg nao esta aqui).
  Pedir por indice nao da erro, da outra propriedade: 30 e `ISO_SPEED`, 29 e
  `GUID`. A idade do pixel dentro do backend passou a sair como **limite**
  deduzido do ciclo de entrega, no setimo canal do `CV0_SYNC`.
- `CV0_SRC=ndi` **nao e um teste sem camera**. Nesta maquina existe uma fonte
  virtual chamada `NDI Webcam Video 1`, e ela abre pelo MSMF como qualquer
  captura: a passe de 30 s levou **3 frames** e o backend emitiu
  `CvCapture_MSMF::grabFrame videoio(MSMF): can't grab frame. Error: -2147483638`
  a cada ~10 s. O efeito colateral e uma corrida que parece morta e e so uma
  fonte a ~0,1 Hz; o `pub->bloco` dela deu mediana **3263 ms** com `fora 1294`,
  enquanto o `pub->nota` ficou com `n = 0` porque nenhuma nota chegou ao anel.
  Para uma corrida sem fonte nao ha atalho por variavel de ambiente: a fonte
  tem de falhar mesmo, ou entao usar a webcam com a sala vazia, que e o que o
  controle negativo do passo 62 fez.
