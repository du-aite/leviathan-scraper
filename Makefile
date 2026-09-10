# Leviathan Scraper — Makefile (Fase 3)
# ======================================
# Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
#
# Dois mundos, um codigo. Tres trabalhos:
#
#   make native    -> compila pro TEU computador (Mac/Linux) pra testar rapido.
#                     Roda com `./leviathan_ui <pasta de roms>` a partir da raiz
#                     do projeto (precisa dos assets/ pra achar a fonte etc).
#
#   make brick     -> compila pro aarch64 do Brick, DENTRO do container da
#                     toolchain (arm64-tg3040). Gera o binario que vai pro device.
#                     Define -DTARGET_BRICK, que o codigo usa pro swap A/B.
#
#   make release   -> monta a pasta release/Leviathan/ COMPLETA, pronta pro scp:
#                     binario + launch.sh + config.json + assets/ (fonte,
#                     sistemas.json, cacert.pem). Roda `make brick` antes.
#
#   make clean     -> apaga os binarios e a pasta release montada.
#
# A ideia central, herdada desde o Go: o codigo-fonte e identico nos dois
# alvos. So muda o compilador (e o -D do Brick). A logica nao sabe onde roda.

# --------------------------------------------------------------
# Nomes e caminhos
# --------------------------------------------------------------
NOME_BIN   := leviathan_ui
DIR_SRC    := src
DIR_ASSETS := assets
DIR_REL    := release/Leviathan

# TODOS os fontes da Fase 2 — a mesma lista do `cc` gigante que funcionou.
# Ordem nao importa pro compilador; agrupei por camada pra leitura.
FONTES := \
	$(DIR_SRC)/main.c \
	$(DIR_SRC)/scrape_ui.c \
	$(DIR_SRC)/scan_ui.c \
	$(DIR_SRC)/scrape.c \
	$(DIR_SRC)/sshttp.c \
	$(DIR_SRC)/ssparse.c \
	$(DIR_SRC)/romhash.c \
	$(DIR_SRC)/md5.c \
	$(DIR_SRC)/romscan.c \
	$(DIR_SRC)/strlist.c \
	$(DIR_SRC)/systems.c \
	$(DIR_SRC)/status.c \
	$(DIR_SRC)/credentials.c \
	$(DIR_SRC)/jsmn.c

# Flags comuns aos dois alvos. -std=c99 pra bater com o resto do projeto.
# CFLAGS_EXTRA fica vazio por padrao e serve pra ligar flags pontuais sem
# editar o Makefile, ex:   make brick CFLAGS_EXTRA=-DBTN_DEBUG
CFLAGS_COMUM := -std=c99 -Wall -Wextra -O2 -I$(DIR_SRC) $(CFLAGS_EXTRA)

# Bibliotecas: SDL2 + SDL2_ttf + libcurl, todas via pkg-config.
# SDL2 + SDL2_ttf via pkg-config nos dois alvos (funciona no Mac e no container).
# O curl e tratado SEPARADO por alvo, porque no Brick a decisao da Fase 1 foi:
# usar o HEADER do container mas LINKAR contra a libcurl real do device (em
# devicelibs/), nunca contra a do Debian — a do Debian e mais nova (7.64) e tem
# versionamento de simbolos que a lib do Brick (7.54) nao carrega, o que
# quebraria SO no device, com erro criptico. Ver Fase 1.
PKG_LIBS := sdl2 SDL2_ttf SDL2_image

CFLAGS_PKG := $(shell pkg-config --cflags $(PKG_LIBS))
LDLIBS_PKG := $(shell pkg-config --libs $(PKG_LIBS))

# NATIVO: no Mac o curl vem do pkg-config, simples.
CURL_NATIVE := $(shell pkg-config --cflags --libs libcurl)

# BRICK: header do sistema (instalado via `apt-get install libcurl4-openssl-dev`
# dentro do container) + link contra as .so reais do device em devicelibs/.
# -Wl,-rpath deixa o loader achar as libs; no device elas ficam em /usr/lib,
# mas pro LINK aqui o que vale e o -L devicelibs.
DIR_DEVLIBS := devicelibs
CURL_BRICK  := -L$(DIR_DEVLIBS) -lcurl -lssl -lcrypto -lnghttp2

# SDL2_image no Brick: header do container (libsdl2-image-dev), mas link contra
# a .so real do device. O arquivo em devicelibs/ chama-se libSDL2_image-2.0.so.0
# — o "-2.0.so.0" no meio NAO casa com -lSDL2_image (que procura libSDL2_image.so),
# entao linkamos o arquivo por caminho direto. As dependencias (libpng, libz,
# libjpeg) ja existem no device, entao o loader as acha em runtime.
IMG_BRICK := $(DIR_DEVLIBS)/libSDL2_image-2.0.so.0

# --------------------------------------------------------------
# Alvo NATIVO (teu Mac/Linux) — teste rapido
# --------------------------------------------------------------
# No Mac:    brew install sdl2 sdl2_ttf curl
# No Ubuntu: apt install libsdl2-dev libsdl2-ttf-dev libcurl4-openssl-dev
native: $(FONTES)
	$(CC) $(CFLAGS_COMUM) $(CFLAGS_PKG) $(FONTES) $(LDLIBS_PKG) $(CURL_NATIVE) -o $(NOME_BIN)
	@echo ""
	@echo "OK: binario nativo '$(NOME_BIN)' criado."
	@echo "Rode com:  ./$(NOME_BIN) <pasta de roms>   (a partir desta pasta)"

# --------------------------------------------------------------
# Alvo BRICK (aarch64) — o binario que vai pro device
# --------------------------------------------------------------
# Roda DENTRO do container da toolchain arm64-tg3040, que exporta CROSS_COMPILE
# como /usr/bin/aarch64-linux-gnu-  ->  $(CROSS_COMPILE)gcc.
#
# A DIFERENCA da Fase 3: -DTARGET_BRICK. Esse simbolo e o que o codigo usa pra
# aplicar o swap A/B (os botoes do Brick chegam invertidos vs. o rotulo fisico).
# No native ele NAO e definido, entao o teclado/controle do Mac continua normal.
CC_BRICK := $(CROSS_COMPILE)gcc
CFLAGS_BRICK := $(CFLAGS_COMUM) -DTARGET_BRICK

brick: $(FONTES)
	$(CC_BRICK) $(CFLAGS_BRICK) $(CFLAGS_PKG) $(FONTES) $(LDLIBS_PKG) $(CURL_BRICK) $(IMG_BRICK) -o $(NOME_BIN)
	@echo ""
	@echo "OK: binario aarch64 '$(NOME_BIN)' criado (para o Brick, com -DTARGET_BRICK)."
	@echo "Confira a arquitetura com:  file $(NOME_BIN)   (deve dizer 'ARM aarch64')"

# --------------------------------------------------------------
# Alvo RELEASE — monta a pasta do app pro device
# --------------------------------------------------------------
# Depende de `brick`: sempre empacota o binario do device, nunca o do Mac.
# Monta release/Leviathan/ do zero toda vez, pra nao arrastar sobras de builds
# antigos. Esta e a pasta que vai inteira pro /mnt/SDCARD/Apps/ via scp.
#
# Os tres assets que o app precisa em runtime:
#   silkscreen.ttf  — a fonte da UI
#   sistemas.json   — o cache de sistemas do ScreenScraper
#   cacert.pem      — o CA bundle (o device nao tem CA store proprio)
#   background.png  — a arte de fundo desenhada atras de todas as telas
ASSETS_DEVICE := \
	$(DIR_ASSETS)/silkscreen.ttf \
	$(DIR_ASSETS)/sistemas.json \
	$(DIR_ASSETS)/cacert.pem \
	$(DIR_ASSETS)/background.png

release: brick
	@echo ""
	@echo "montando $(DIR_REL)/ ..."
	rm -rf $(DIR_REL)
	mkdir -p $(DIR_REL)/assets
	cp $(NOME_BIN) $(DIR_REL)/leviathan
	cp launch.sh $(DIR_REL)/launch.sh
	cp config.json $(DIR_REL)/config.json
	cp $(DIR_ASSETS)/icon.png $(DIR_REL)/icon.png
	cp $(ASSETS_DEVICE) $(DIR_REL)/assets/
	@# credentials.txt goes at the app ROOT (next to the binary), not under
	@# assets/, so the user can find and edit it easily — the app reads it from
	@# there via LEV_CREDENTIALS_FILE. It is the blank template with placeholders;
	@# the user fills in their own ScreenScraper login on first run.
	cp credentials.txt $(DIR_REL)/credentials.txt
	@echo ""
	@echo "OK: pacote pronto em $(DIR_REL)/"
	@echo "conteudo:"
	@ls -1 $(DIR_REL) $(DIR_REL)/assets | sed 's/^/    /'
	@echo ""
	@echo "Pro device (do Mac):"
	@echo "  ssh root@192.168.100.110 \"mkdir -p /mnt/SDCARD/Apps/Leviathan\""
	@echo "  scp -r $(DIR_REL)/. root@192.168.100.110:/mnt/SDCARD/Apps/Leviathan/"
	@echo ""
	@echo "AVISO: este pacote traz um credentials.txt EM BRANCO (so placeholders)."
	@echo "Se voce fizer scp por cima de um device onde o usuario ja preencheu o"
	@echo "login, o arquivo dele sera SOBRESCRITO. Ao atualizar um app ja instalado,"
	@echo "considere nao copiar o credentials.txt por cima (copie o resto)."

# --------------------------------------------------------------
# Limpeza
# --------------------------------------------------------------
clean:
	rm -f $(NOME_BIN)
	rm -rf $(DIR_REL)
	@echo "limpo."

.PHONY: native brick release clean
