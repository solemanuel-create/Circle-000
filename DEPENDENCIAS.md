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
| openFrameworks 0.12.1 | `libs/openFrameworksCompiled/lib/vs/x64/openframeworksLib.lib` | via `ProjectReference` (`vcxproj:278`) e as `.props` importadas em `vcxproj:59` e `:63` | o rebuild do core (lento) se os fontes dele mudarem |
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
de `:267-275`, `<ClInclude .../>` no grupo de `:278-287` e, para o VS nao mostrar
tudo solto, o mesmo arquivo em `ctrlv0.vcxproj.filters`. Os tres grupos
estao completos desde o P4: nove `.cpp` (blobModel, cvDevices, effectTable,
kinect1473, main, maskPipeline, ofApp, paramTable, sourceTable) e dez headers
(blobModel, cvDevices, cvModel, cvSpace, effectTable, kinect1473, maskPipeline,
ofApp, paramTable, sourceTable). A queixa antiga - "o filters so lista ofApp.cpp,
main.cpp e ofApp.h" - nao vale mais. O que vale e o risco: um arquivo novo no
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
- Fora do projeto, mas no caminho do pacote de audio: `audiosrv.dll`, o servico
  de audio do Windows, quebrou 67 vezes em dois dias com `0xc0000005`, em ciclo
  de poucos minutos (contado no Application Log em 2026-11-06). Som em cena
  depende desse servico, nao so do nosso `ofSoundStream`.
