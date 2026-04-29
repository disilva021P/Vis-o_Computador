# 🍊 Visão por Computador – Trabalho Prático
### Deteção e Classificação de Citrinos em Vídeo

**Instituto Politécnico do Cávado e do Ave**  
Escola Superior de Tecnologia  
Engenharia de Sistemas Informáticos – 2º Ano  
Ano Letivo 2025/2026

---

## 👥 Grupo

**Grupo:** 10

| Nome              | Nº Aluno |
|-------------------|----------|
| Diogo Silva      | 31504     |
| Rodrigo Miranda      | 31509     |
| Rui Barbosa      | 31515     |
| Hugo Carvalho      | 31519     |

---

## 📋 Descrição

Este projeto foi desenvolvido no âmbito da unidade curricular de **Visão por Computador** e tem como objetivo processar o vídeo `video.avi` para detetar, classificar e analisar laranjas em tempo real, frame a frame.

O programa foi desenvolvido em **C/C++** com recurso à biblioteca **OpenCV** (limitadamente).

---

## 🎯 Funcionalidades Implementadas

Para cada frame do vídeo, o programa exibe:

- ✅ **Contagem total** de laranjas desde o início do vídeo
- ✅ **Número de laranjas** detetadas na frame atual
- ✅ **Área e perímetro** de cada laranja
- ✅ **Localização** (bounding box) e **centro de gravidade** de cada laranja
- ✅ **Calibre** das laranjas (conforme Normas de Qualidade para os Citrinos – Secção III)
- ✅ **Categoria** de cada laranja (conforme Normas de Qualidade para os Citrinos – Secção II, ponto D)

---

## 🛠️ Tecnologias e Ferramentas

- **Linguagem:** C/C++
- **Biblioteca:** OpenCV
- **Compilador:** g++ / MSVC

---

## ⚙️ Compilação e Execução

### Pré-requisitos

- OpenCV instalado
- Compilador C++ compatível

---

## 📁 Estrutura do Projeto

```
VC-GX-XXXX-XXXX-XXXX-XXXX/
│
├── main.cpp          # Ficheiro principal
├── vc.h              # Header com estruturas e protótipos
├── vc.cpp            # Implementação das funções de visão por computador
└── README.md         # Este ficheiro
```

---

## 📐 Informações Técnicas do Vídeo

| Parâmetro        | Valor         |
|------------------|---------------|
| Resolução        | 1280 × 720 px |
| Frame rate       | 25 fps        |
| Escala           | 280 px = 55 mm |

---

## 📦 Entrega

O ficheiro zip de entrega segue o formato:

```
VC-G10-31504-31509-31515-31519.zip
```

**Prazo:** 18 de maio, às 23:00

---

## ⚖️ Conduta Ética

Todo o código presente neste repositório foi desenvolvido exclusivamente pelos elementos do grupo. Todas as fontes externas utilizadas estão devidamente referenciadas. Qualquer plágio ou cópia implicará a anulação do trabalho.
