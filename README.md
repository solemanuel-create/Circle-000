# Ctrl-v-0

Instrumento audiovisual ao vivo. Uma camera (webcam integrada, Kinect 360
model 1473, EOS Webcam Utility ou NDI) entrega um frame; o frame vira
silhueta; a silhueta vira um feixe de sinais (energia, densidade, velocidade
do centro de massa, atividade por zona, coerencia entre zonas, nivel
ambiente); os sinais movem **imagem e som no mesmo instante** porque um unico
motor de envelope os alimenta. A saida depende do que acontece na sala, nao
do que alguem escolhe num menu.

O render e GPU (`ofFbo` + `ofShader`, GL 3.3) sobre `bin/data/shadersGL3/blobfx.*`,
com tres efeitos (invert, bw umbral, bw dither). O rastreio roda numa maquina
de 160-1280 px de largura, nunca na resolucao da tela.

## Abrir

- **duplo clique no atalho `Ctrl-v-0`** - saida limpa, consola minimizada.
- **`Abrir-Ctrl-v-0.bat [fonte]`** - abre com a consola de diagnostica visivel.
  O argumento opcional escolhe a fonte no `setup()`: `webcam`, `kinect`, `eos`,
  `ndi`. Sem argumento o app usa a fonte que o codigo dele escolhe sozinho.
- o exe e `bin\ctrlv0.exe`. Debug|x64 gera `bin\ctrlv0_debug.exe`.

**A diagnostica sai por stderr, nao pelo log do openFrameworks.** Nesta build a
*escrita* do log do OF derruba o processo em `c0000005` na primeira linha
escrita - medido, nao presumido (ver `DEPENDENCIAS.md` secao 0). Por isso `main.cpp`
poe `OF_LOG_SILENT` e todo aviso do projeto passa por `cv0Log`. Nenhuma
dependencia nova pode logar ate a causa raiz ser achada.

## Compilar

```
MSBuild ctrlv0.vcxproj -p:Configuration=Release -p:Platform=x64 -p:BuildProjectReferences=false
```

So **x64** esta completo: os includes de addon so existem sob `'$(Platform)'=='x64'`
e as bibliotecas so aparecem em Debug|x64 e Release|x64 (`DEPENDENCIAS.md` secao 2).
A saida vai para `bin\` e o `bin\data\` e lido relativo ao exe - shaders, a foto
`0013.jpg` e os presets estao la.

## Teclas

O painel direito do HUD e gerado a partir da mesma tabela que as teclas usam
(`src/paramTable.cpp`), com valor, faixa e tecla por linha.

| tecla | o que faz |
|---|---|
| `F1` `F2` `F3` `F4` | fonte: webcam integrada, kinect 360, EOS utility, NDI |
| `1` `2` `3` | efeito: invert, bw umbral, bw dither |
| `c` | backend DSHOW/MSMF (so nas fontes por `cv::VideoCapture`) |
| `v` | modo pedido ao sensor (o 1473 e fixo 640x480) |
| `,` `.` | borda PERTO da porta de distancia (400-3000 mm) |
| `[` `]` | borda LONGE (teto 4000 mm = `NUI_IMAGE_DEPTH_MAXIMUM`); com webcam, limiar de movimento |
| `z` `x` | largura da maquina de rastreio (160-1280) |
| `+` `-` | umbral / dither bias |
| `g` | liga/desliga a reacao |
| `n` `m` | master dos ganhos -/+ 0,25 (0..2) |
| `t` | ver a maquina (a binarizada no lugar do frame) |
| `d` | overlay dos retangulos por cima da saida |
| `f` | flipY |
| `b` | refaz o fundo | 
| `r` | reseed do baralho de retangulos |
| `a` | novo retangulo |
| `p` | dialog de ISO/exposicao do driver |
| `s` | salva `cv0_<frame>.png` |
| `u` | mudo do som **por rampa de 0,12 s**, nunca por ganho a zero no meio do periodo; a rota ASIO fica aberta e o motorista continua a vir |

Tecla de fonte que nao muda nada nao paga uma ligacao: reabrir o que ja esta
entregando frame seria ate 25 s de sensor morto no meio de um show. A reserva
e a fonte parada - com frame parado a mesma tecla vira o reabrir manual.

## O que o HUD le

Coluna esquerda, so medicao: fps, blobs, fonte atual e contador de ligacoes
pagas, taxa medida de cor e profundidade, paralaxe dentro/fora/falha,
`espera camera | rastreio | ciclo` em ms, os seis sinais com o custo do
barramento, as seis zonas 3x2, a linha do som (`transporte | pico | ag | vozes |
ceif | perd | custo do callback`) e os cinco ganhos de tela ja aplicados
(`master x ganho x envelope`). Coluna direita: a tabela de parametros e os
comandos. Avisos descem para o pe da janela.

## Estado honesto

Provado em runtime neste hardware: render GPU com os tres efeitos, rastreio por
movimento na integrada (~30 fps, MSMF), Kinect 1473 real pelo NuiApi SDK v1.8
(cor e profundidade a ~30 fps em 640x480, silhueta por porta de distancia com
mapa de paralaxe completo), abertura assincrona do sensor, barramento de sinais
a 0,03 ms, saida MIDI de controle pelo cabo `Live TD`, e o motor de sintetese na
rota ASIO4ALL (`466-473 callbacks em 5 s`, `underflow 0`, `0,11-0,17 ms` por
callback contra um orcamento de 10,7 ms, pico medido pelo proprio motorista a
`0,0000` no mudo) - com a tabela offline das doze cenas e o espectro do WAV
analisados por fora, cena a cena. O P6 (o renomear do artefato) esta provado.

Ainda nao provado: os cues e os perfis `registro` / `performance`, a janela dupla
de saida, a rota EOS com o dispositivo presente e uma silhueta registrada com um
corpo a distancia conhecida. **Uma segunda instancia do exe morre ao abrir o
ASIO** (reproduzido duas vezes, documentado em `DEPENDENCIAS.md` seccao 8) - o
palco usa uma. As teclas que mudam algo de verdade so podem ser provadas por mao
no teclado - injecao sintetica nao chega ao GLFW nesta plataforma.

`DEPENDENCIAS.md` e o registro do que este projeto linka, das configuracoes
incompletas, das variaveis de ambiente, da receita de build e das armadilhas ja
pagas. Leia antes de mexer.
