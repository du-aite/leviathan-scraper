// Leviathan Scraper — versão Brick (C + SDL2)
// ============================================
// FASE 0 — Esqueleto.
//
// O objetivo desta fase NÃO é fazer o scraper. É provar, no teu device real,
// as duas coisas que a Fyne nos dava de graça e agora fazemos na mão:
//   1) desenhar texto na tela (SDL2 + SDL2_ttf, com a fonte pixelizada);
//   2) ler os botões físicos (SDL GameController) e reagir a eles.
//
// Se isto compilar na toolchain arm64 e rodar no Brick mostrando o texto e
// respondendo ao A/B/D-pad, a fundação está de pé e as Fases 1–3 têm chão.
//
// Nada aqui fala com a internet, lê ROM ou baixa capa. É de propósito.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>

// ── Resolução do Brick Pro ──
// A mesma que a GUI Fyne mirava (janela.Resize 1024x768).
#define LARGURA 1024
#define ALTURA  768

// ── Cores ──
// Você pediu azul. Este é um azul "terminal", claro o bastante pra ler
// sobre fundo escuro. (No RGUI do RetroArch o texto é verde; aqui é a
// nossa identidade.)
static const SDL_Color AZUL   = { 90, 170, 255, 255 };  // texto principal
static const SDL_Color CINZA  = { 120, 120, 120, 255 }; // texto secundário
static const SDL_Color FUNDO  = { 12, 14, 20, 255 };    // fundo quase-preto

// ── Fonte ──
// Silkscreen: fonte bitmap/pixel, licença SIL OFL (uso livre, embarcável).
// Fica ao lado do binário, dentro da pasta do App. O caminho é relativo
// ao diretório de trabalho que o launch.sh define (cd pra pasta do app).
#define CAMINHO_FONTE "assets/silkscreen.ttf"
#define TAM_FONTE_TITULO 48
#define TAM_FONTE_TEXTO  24

// desenharTexto renderiza uma string e a "carimba" na tela na posição (x,y).
// É a operação mais básica da casca: em Fyne era um widget.Label; aqui é
// isto. Todo o resto da UI vai ser composto a partir desta função.
//
// Repara no ciclo: cria surface (pixels na RAM) -> texture (pixels na GPU)
// -> copia pro renderer -> libera os dois. É verboso, mas é o preço de não
// ter um toolkit fazendo por baixo dos panos.
static void desenharTexto(SDL_Renderer *ren, TTF_Font *fonte,
                          const char *texto, int x, int y, SDL_Color cor) {
    if (texto == NULL || texto[0] == '\0') {
        return; // nada a desenhar
    }

    SDL_Surface *surf = TTF_RenderUTF8_Blended(fonte, texto, cor);
    if (surf == NULL) {
        fprintf(stderr, "TTF_Render falhou: %s\n", TTF_GetError());
        return;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex == NULL) {
        fprintf(stderr, "CreateTexture falhou: %s\n", SDL_GetError());
        SDL_FreeSurface(surf);
        return;
    }

    SDL_Rect destino = { x, y, surf->w, surf->h };
    SDL_RenderCopy(ren, tex, NULL, &destino);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// desenharTextoCentrado é um açúcar sobre desenharTexto: mede a string e a
// posiciona horizontalmente no meio da tela. Útil pro título da splash.
static void desenharTextoCentrado(SDL_Renderer *ren, TTF_Font *fonte,
                                  const char *texto, int y, SDL_Color cor) {
    int w = 0, h = 0;
    if (TTF_SizeUTF8(fonte, texto, &w, &h) != 0) {
        return;
    }
    desenharTexto(ren, fonte, texto, (LARGURA - w) / 2, y, cor);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv; // ainda não usamos argumentos

    // ── Inicialização do SDL ──
    // VIDEO pra tela; GAMECONTROLLER pra ler os botões físicos do Brick.
    // O device entrega os botões como eventos de gamepad (via o
    // gamecontrollerdb.txt do firmware), então é este subsistema que importa.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init falhou: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init falhou: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    // ── Janela + renderer ──
    SDL_Window *janela = SDL_CreateWindow(
        "Leviathan Scraper",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LARGURA, ALTURA,
        SDL_WINDOW_SHOWN);
    if (janela == NULL) {
        fprintf(stderr, "CreateWindow falhou: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        janela, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (ren == NULL) {
        fprintf(stderr, "CreateRenderer falhou: %s\n", SDL_GetError());
        SDL_DestroyWindow(janela); TTF_Quit(); SDL_Quit();
        return 1;
    }

    // ── Fontes ──
    // Carregamos a mesma fonte em dois tamanhos: um pro título, um pro corpo.
    TTF_Font *fonteTitulo = TTF_OpenFont(CAMINHO_FONTE, TAM_FONTE_TITULO);
    TTF_Font *fonteTexto  = TTF_OpenFont(CAMINHO_FONTE, TAM_FONTE_TEXTO);
    if (fonteTitulo == NULL || fonteTexto == NULL) {
        fprintf(stderr, "OpenFont falhou (%s): %s\n", CAMINHO_FONTE, TTF_GetError());
        // Segue mesmo assim seria inútil sem fonte; abortamos limpo.
        if (fonteTitulo) TTF_CloseFont(fonteTitulo);
        if (fonteTexto)  TTF_CloseFont(fonteTexto);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(janela);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    // ── Abrir o primeiro gamepad disponível ──
    // No Brick há um só "controller" (os botões do próprio aparelho).
    // Abrimos o índice 0 se ele for reconhecido como GameController.
    SDL_GameController *controle = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controle = SDL_GameControllerOpen(i);
            if (controle != NULL) {
                break;
            }
        }
    }
    // Não é fatal se não abrir: no desktop (teu Mac) pode não haver controle,
    // e aí a gente testa pelo teclado (mapeado mais abaixo). No Brick ele
    // deve aparecer. A tela mostra o estado pra você conferir na hora.

    // ── Estado da Fase 0 ──
    // Só o suficiente pra provar que o input chega: a última tecla/botão
    // apertado e um contador de "confirmações" (A).
    const char *ultimoBotao = "(nenhum)";
    int vezesA = 0;

    // ── Loop principal ──
    // O coração de qualquer app SDL: processa eventos -> desenha -> repete.
    // Em Fyne isso era escondido; aqui é explícito, e é exatamente onde a
    // navegação por d-pad vai morar nas próximas fases.
    bool rodando = true;
    while (rodando) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                rodando = false;
                break;

            // Botões do gamepad (o caminho do Brick)
            case SDL_CONTROLLERBUTTONDOWN:
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                    ultimoBotao = "A (confirmar)";
                    vezesA++;
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    ultimoBotao = "B (voltar/sair)";
                    rodando = false; // na Fase 0, B sai
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    ultimoBotao = "D-pad CIMA";
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    ultimoBotao = "D-pad BAIXO";
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    ultimoBotao = "D-pad ESQUERDA";
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    ultimoBotao = "D-pad DIREITA";
                    break;
                default:
                    ultimoBotao = "(outro botao)";
                    break;
                }
                break;

            // Teclado (o caminho do teu Mac, pra testar sem controle)
            // No Brick isto provavelmente nem dispara, mas não atrapalha.
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_RETURN:
                    ultimoBotao = "ENTER (= A)";
                    vezesA++;
                    break;
                case SDLK_ESCAPE:
                    ultimoBotao = "ESC (= B)";
                    rodando = false;
                    break;
                case SDLK_UP:    ultimoBotao = "SETA CIMA";     break;
                case SDLK_DOWN:  ultimoBotao = "SETA BAIXO";    break;
                case SDLK_LEFT:  ultimoBotao = "SETA ESQUERDA"; break;
                case SDLK_RIGHT: ultimoBotao = "SETA DIREITA";  break;
                default: break;
                }
                break;

            default:
                break;
            }
        }

        // ── Desenho ──
        // 1. Limpa a tela com o fundo escuro.
        SDL_SetRenderDrawColor(ren, FUNDO.r, FUNDO.g, FUNDO.b, FUNDO.a);
        SDL_RenderClear(ren);

        // 2. Título.
        desenharTextoCentrado(ren, fonteTitulo, "LEVIATHAN SCRAPER", 120, AZUL);
        desenharTextoCentrado(ren, fonteTexto, "v1.0.0  -  Fase 0 (esqueleto)", 190, CINZA);

        // 3. Diagnóstico de input — a prova real desta fase.
        char linha[128];

        snprintf(linha, sizeof(linha), "Controle detectado: %s",
                 controle ? SDL_GameControllerName(controle) : "(nenhum - use o teclado)");
        desenharTexto(ren, fonteTexto, linha, 80, 320, CINZA);

        snprintf(linha, sizeof(linha), "Ultimo input: %s", ultimoBotao);
        desenharTexto(ren, fonteTexto, linha, 80, 370, AZUL);

        snprintf(linha, sizeof(linha), "Vezes que apertou A: %d", vezesA);
        desenharTexto(ren, fonteTexto, linha, 80, 420, AZUL);

        // 4. Rodapé com a legenda dos botões (a semente da "hint bar" futura).
        desenharTexto(ren, fonteTexto, "A: confirmar    B: sair", 80, ALTURA - 80, CINZA);

        // 5. Apresenta o quadro montado.
        SDL_RenderPresent(ren);
    }

    // ── Limpeza ──
    // Ordem inversa da criação. C não tem 'defer'; a disciplina é nossa.
    if (controle != NULL) {
        SDL_GameControllerClose(controle);
    }
    TTF_CloseFont(fonteTexto);
    TTF_CloseFont(fonteTitulo);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(janela);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
