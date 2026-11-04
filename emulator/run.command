#!/bin/bash
cd "$(dirname "$0")"   # теперь это уже корень проекта
python3 main.py
read -p "Нажмите Enter для выхода..."
