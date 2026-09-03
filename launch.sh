#!/bin/sh
# Leviathan Scraper — launcher para o stock OS do Trimui Brick
# ============================================================
# O menu "Apps" do stock OS executa este script quando você seleciona
# o Leviathan. A função dele é simples mas essencial:
#
#   1. Descobrir em que pasta ele mesmo está (a pasta do App no cartão).
#   2. Entrar nessa pasta (cd), pra que o binário ache os assets pelo
#      caminho relativo "assets/silkscreen.ttf" que usamos no main.c.
#   3. Rodar o binário.
#
# Sem o passo 2, o app rodaria com o diretório de trabalho errado e não
# encontraria a fonte — a tela abriria preta. É a pegadinha clássica de
# app de device retro, então tratamos ela logo de cara.

# Pasta onde ESTE script está (= pasta do App). Resolve mesmo se o menu
# chamar por caminho relativo.
DIR_APP="$(dirname "$0")"
cd "$DIR_APP" || exit 1

# Log — no stock OS ajuda muito ter um registro do que aconteceu, já que
# não há terminal visível. Se algo falhar, este arquivo conta a história.
LOG="./leviathan.log"

echo "=== Leviathan Scraper — $(date) ===" > "$LOG"
echo "Pasta do app: $DIR_APP" >> "$LOG"

# Roda o binário, mandando saída normal e de erro pro log.
./leviathan >> "$LOG" 2>&1

echo "=== Fim (codigo de saida: $?) ===" >> "$LOG"
