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
