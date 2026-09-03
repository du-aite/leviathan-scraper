# Leviathan Scraper — Makefile (Fase 0)
# ======================================
# Dois mundos, um código:
#
#   make native   -> compila pro TEU computador (Mac/Linux) pra testar rápido.
#                    Roda com `./leviathan` na mesma pasta (precisa dos assets).
#
#   make brick    -> compila pro aarch64 do Brick, DENTRO do container da
#                    toolchain (arm64-tg3040). Gera o binário que vai pro device.
#
# Nota sobre o nome do device: a toolchain se chama "tg3040" e o alvo aqui e
# "brick" — os dois servem pro Brick Pro (TG4040), que e aarch64 e tem o mesmo
# processador do TG3040. A arquitetura e o que importa pro binario; o numero do
# modelo nao muda o codigo de maquina.
#
# A ideia central: o codigo-fonte e identico nos dois alvos. So muda o
# compilador. E o mesmo principio do miolo Go — a logica nao sabe onde roda.

NOME_BIN   := leviathan
DIR_SRC    := src
FONTES     := $(DIR_SRC)/main.c

# Flags de compilacao comuns aos dois alvos.
CFLAGS_COMUM := -Wall -Wextra -O2

# --------------------------------------------------------------
# Alvo NATIVO (teu Mac/Linux) — teste rapido
# --------------------------------------------------------------
# Usa pkg-config pra achar SDL2 e SDL2_ttf instalados no sistema.
# No Mac:    brew install sdl2 sdl2_ttf
# No Ubuntu: apt install libsdl2-dev libsdl2-ttf-dev
native: $(FONTES)
	$(CC) $(CFLAGS_COMUM) $(FONTES) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf) \
		$(shell pkg-config --libs sdl2 SDL2_ttf) \
		-o $(NOME_BIN)
	@echo ""
	@echo "OK: binario nativo '$(NOME_BIN)' criado."
	@echo "Rode com:  ./$(NOME_BIN)   (a partir desta pasta, pros assets serem achados)"

# --------------------------------------------------------------
# Alvo BRICK (aarch64) — o binario que vai pro device
# --------------------------------------------------------------
# Roda DENTRO do container da toolchain arm64-tg3040. La dentro confirmamos:
#   - compilador:  aarch64-linux-gnu-gcc  (via a var CROSS_COMPILE do container)
#   - SDL2 dev:     OK  (pkg-config acha sozinho)
#   - SDL2_ttf dev: OK
# Por isso o alvo brick e quase identico ao native: so troca o compilador.
# Usamos $(CROSS_COMPILE)gcc, que o container ja exporta como
# /usr/bin/aarch64-linux-gnu- — entao vira /usr/bin/aarch64-linux-gnu-gcc.
CC_BRICK := $(CROSS_COMPILE)gcc

brick: $(FONTES)
	$(CC_BRICK) $(CFLAGS_COMUM) $(FONTES) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf) \
		$(shell pkg-config --libs sdl2 SDL2_ttf) \
		-o $(NOME_BIN)
	@echo ""
	@echo "OK: binario aarch64 '$(NOME_BIN)' criado (para o Brick)."
	@echo "Confira a arquitetura com:  file $(NOME_BIN)"

# --------------------------------------------------------------
# Limpeza
# --------------------------------------------------------------
clean:
	rm -f $(NOME_BIN)
	@echo "limpo."

.PHONY: native brick clean
