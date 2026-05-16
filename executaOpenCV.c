// ============================================================
//  executaOpenCV.c
//  Contagem e classificação de laranjas em vídeo.
//
//  Pipeline por frame:
//    1) BGR -> HSV
//    2) Segmentação HSV (isola píxeis laranja)
//    3) Dilatação + Erosão (fecha buracos e remove ruído)
//    4) Labeling de blobs
//    5) Para cada blob válido:
//         a) Calcula diâmetro, calibre, categoria
//         b) Deteta defeitos (píxeis não-laranja dentro do blob)
//         c) Verifica aprovação (diâmetro >= 53 mm)
//         d) Rastreia entre frames e conta ao cruzar a linha
//    6) Verifica homogeneidade de calibre (Regulamento CEE 379/71, III.c)
// ============================================================

#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>
#include <vector>
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>

extern "C" {
#include "vc.h"
#include "executaOpenCV.h"
}

// ============================================================
//  Constantes  
// ============================================================

// Fator de conversão píxeis -> milímetros (280 px = 55 mm)
#define MM_PER_PIXEL      (55.0f / 280.0f)

// Ordenada (em píxeis) da linha de contagem.
// Uma laranja é contada quando o topo da bounding-box ultrapassa
// esta linha pela primeira vez.
#define LINHA_CONTAGEM    40

// Área mínima de um blob para não ser considerado ruído
#define AREA_MIN          25000

// Distância máxima entre centros para associar um blob a um track
#define DIST_MAX          150.0f

// Número de frames consecutivos sem deteção para remover um track
#define FRAMES_AUSENTE    15

// Diâmetro mínimo para aprovação comercial (Regulamento CEE 379/71)
#define DIAM_MIN_MM       53.0f

// Percentagem de píxeis não-laranja acima da qual o fruto tem defeito
#define DEFEITO_THRESHOLD 0.10f

// ============================================================
//  Estruturas de dados
// ============================================================

// Registo permanente de uma laranja já contada (após cruzar a linha).
struct LaranjaInfo {
    int   id;
    float diametro_mm;
    int   calibre;         // calibre (0-13) ou -1 se abaixo do mínimo
    char  categoria[16];   // "Extra", "I", "II/III" ou "Fora"
    int   area;
    int   perimetro;
    int   cx, cy;          // centro de gravidade (atualizado frame a frame)
    bool  aprovada;
    float pct_defeito;
    // Bounding-box congelada no momento da contagem
    int   bbox_w, bbox_h;
};

// Estado de rastreio de um blob entre frames consecutivos.
struct BlobTrack {
    int   cx, cy;
    int   blobY;           // topo da bounding-box
    int   frameVista;      // última frame em que foi detetado
    bool  contado;
    int   laranjaId;       // id da LaranjaInfo correspondente (-1 se ainda não contado)
    float diametro_mm;
    int   calibre;
    char  categoria[16];
    int   area;
    int   perimetro;
    bool  aprovada;
    float pct_defeito;
    int   bbox_w, bbox_h;
};

// ============================================================
//  Protótipos internos
// ============================================================

static float        calcDiametroMM      (int bboxW, int bboxH);
static int          calcCalibre         (float diametro_mm);
static const char*  calcCategoria       (int area, int perimetro, float pct_defeito,
                                         float diamMin, float diamMax);
static float        calcDefeito         (IVC* imageDilated, IVC* imageMask, const OVC& blob);
static LaranjaInfo  criarInfo           (int id, float diam_mm, int calibre,
                                         const char* cat, const OVC& blob,
                                         int cx, int cy, bool aprovada, float pct_defeito);
static void         logContagem         (const LaranjaInfo& info);
static cv::Scalar   corCategoria        (const char* cat);
static void         putTextOutlined     (cv::Mat& img, const char* txt,
                                         cv::Point pos, double scale, cv::Scalar cor);
static void         desenhaHUD          (cv::Mat& frame, int nframe, int ntotalframes,
                                         int nlaranjas, int nlaranjaframe,
                                         int width, int height);
static bool         processaBlob        (cv::Mat& frame, const OVC& blob, int nframe,
                                         IVC* imageDilated, IVC* imageMask,
                                         std::vector<BlobTrack>& rastreio,
                                         std::vector<LaranjaInfo>& laranjas,
                                         int& proximoId,
                                         float diamMinFrame, float diamMaxFrame);
static void         imprimeResumo       (const std::vector<LaranjaInfo>& laranjas);

// ============================================================
//  Classificação — Regulamento CEE 379/71
// ============================================================

// Diâmetro estimado em mm a partir das dimensões da bounding-box.
static float calcDiametroMM(int bboxW, int bboxH)
{
    return ((bboxW + bboxH) / 2.0f) * MM_PER_PIXEL;
}

// Calibre (0-13) conforme tabela do Regulamento CEE 379/71, secção III.
// Devolve -1 se o diâmetro for inferior ao mínimo de 53 mm.
static int calcCalibre(float diametro_mm)
{
    if (diametro_mm >= 100.0f) return  0;
    if (diametro_mm >=  87.0f) return  1;
    if (diametro_mm >=  84.0f) return  2;
    if (diametro_mm >=  81.0f) return  3;
    if (diametro_mm >=  77.0f) return  4;
    if (diametro_mm >=  73.0f) return  5;
    if (diametro_mm >=  70.0f) return  6;
    if (diametro_mm >=  67.0f) return  7;
    if (diametro_mm >=  64.0f) return  8;
    if (diametro_mm >=  62.0f) return  9;
    if (diametro_mm >=  60.0f) return 10;
    if (diametro_mm >=  58.0f) return 11;
    if (diametro_mm >=  56.0f) return 12;
    if (diametro_mm >=  53.0f) return 13;
    return -1;
}

// Categoria com base na circularidade, defeito de superfície e homogeneidade.
// Fórmula circularidade: circ = (4 * PI * area) / (perimetro^2)
// Sistema de pontos:
//   Forma    : >= 0.90 -> 0p | >= 0.80 -> 1p | >= 0.50 -> 2p | < 0.50 -> Fora imediato
//   Superfície: < 0.05 -> 0p | < 0.15 -> 1p  | < 0.30 -> 2p  | >= 0.30 -> 3p
//   Total: <= 1 -> Extra | <= 3 -> I | <= 5 -> II/III | > 5 -> Fora
static const char* calcCategoria(int area, int perimetro, float pct_defeito,
                                  float diamMin, float diamMax)
{
    if (perimetro == 0) return "?";

    float circ = (4.0f * (float)M_PI * (float)area)
               / ((float)perimetro * (float)perimetro);

    if (circ < 0.53f) return "Fora";

    int pontos = 0;

    // Defeito de forma — circularidade
    if      (circ >= 0.90f) pontos += 0;
    else if (circ >= 0.80f) pontos += 1;
    else                    pontos += 2;

    // Defeito de superfície
    if      (pct_defeito < 0.05f) pontos += 0;  
    else if (pct_defeito < 0.15f) pontos += 1;
    else if (pct_defeito < 0.30f) pontos += 2;
    else                          pontos += 3;

    // Homogeneidade de calibre — Regulamento CEE 379/71, Secção III.c
    // Limiar por faixa do maior fruto: calibres 0-2 -> 11 mm, 3-6 -> 9 mm, 7+ -> 7 mm
    if (diamMin <= diamMax && diamMax > 0.0f) {
        int cal   = calcCalibre(diamMax);
        if (cal >= 0) {
            float lim = (cal <= 2) ? 11.0f : (cal <= 6) ? 9.0f : 7.0f;
            if (diamMax - diamMin > lim) pontos += 2; 
        }
    }

    if      (pontos <= 1) return "Extra";
    else if (pontos <= 3) return "I";
    else if (pontos <= 5) return "II/III";
    else                  return "Fora";
}

// ============================================================
//  Deteção de defeitos
// ============================================================

// Estima a percentagem de defeito comparando a máscara dilatada
// (laranja preenchida) com a máscara original (laranja real).
// Fórmula: defeito% = (área_dilatada - área_original) / área_dilatada
// Os píxeis preenchidos pela dilatação mas ausentes na original
// correspondem a buracos e manchas internas.
static float calcDefeito(IVC* imageDilated, IVC* imageMask, const OVC& blob)
{
    unsigned char* dataDil  = imageDilated->data;
    unsigned char* dataMask = imageMask->data;
    int bpl  = imageDilated->bytesperline;
    int ch   = imageDilated->channels;
    int imgW = imageDilated->width;
    int imgH = imageDilated->height;

    // Limitar a bounding-box aos limites da imagem
    int x0 = blob.x;               if (x0 < 0)    x0 = 0;
    int y0 = blob.y;               if (y0 < 0)    y0 = 0;
    int x1 = blob.x + blob.width;  if (x1 > imgW) x1 = imgW;
    int y1 = blob.y + blob.height; if (y1 > imgH) y1 = imgH;

    long int areaDilatada = 0;
    long int areaSemDilat = 0;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            long int pos = y * bpl + x * ch;
            if (dataDil [pos] != 0) areaDilatada++;
            if (dataMask[pos] != 0) areaSemDilat++;
        }
    }

    if (areaDilatada == 0) return 0.0f;
    return (float)(areaDilatada - areaSemDilat) / (float)areaDilatada;
}

// ============================================================
//  Funções OpenCV de desenho e texto
// ============================================================

// Texto com contorno preto para legibilidade sobre qualquer fundo.
// Usa cv::putText (função OpenCV nº 1).
static void putTextOutlined(cv::Mat& img, const char* txt,
                            cv::Point pos, double scale, cv::Scalar cor)
{
    cv::putText(img, txt, pos, cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0,0,0), 2);
    cv::putText(img, txt, pos, cv::FONT_HERSHEY_SIMPLEX, scale, cor,              1);
}

// Cor BGR associada a cada categoria.
static cv::Scalar corCategoria(const char* cat)
{
    if (strcmp(cat, "Extra")  == 0) return cv::Scalar(  0, 255,   0);
    if (strcmp(cat, "I")      == 0) return cv::Scalar(255, 255,   0);
    if (strcmp(cat, "II/III") == 0) return cv::Scalar(  0, 165, 255);
    return cv::Scalar(128, 128, 128);
}

// ============================================================
//  Rastreio — funções auxiliares
// ============================================================

// Preenche um LaranjaInfo com os dados calculados para o blob.
static LaranjaInfo criarInfo(int id, float diam_mm, int calibre,
                             const char* cat, const OVC& blob,
                             int cx, int cy, bool aprovada, float pct_defeito)
{
    LaranjaInfo info;
    info.id          = id;
    info.diametro_mm = diam_mm;
    info.calibre     = calibre;
    strncpy(info.categoria, cat, sizeof(info.categoria) - 1);
    info.area        = blob.area;
    info.perimetro   = blob.perimeter;
    info.cx          = cx;
    info.cy          = cy;
    info.aprovada    = aprovada;
    info.pct_defeito = pct_defeito;
    info.bbox_w      = blob.width;
    info.bbox_h      = blob.height;
    return info;
}

// Imprime no terminal a informação de uma laranja no momento da contagem.
static void logContagem(const LaranjaInfo& info)
{
    std::cout << "[CONTAGEM] Laranja #" << info.id
              << "  Cat:"   << info.categoria
              << "  D:"     << info.diametro_mm << "mm"
              << "  Cal:"   << info.calibre
              << "  A:"     << info.area
              << "  P:"     << info.perimetro
              << "  Aprov:" << (info.aprovada ? "SIM" : "NAO")
              << "  Def:"   << (int)(info.pct_defeito * 100) << "%"
              << "\n";
}

// ============================================================
//  Processamento de cada blob detetado
// ============================================================

// Processa um blob: calcula métricas, associa a um track (ou cria um
// novo), conta ao cruzar a linha de contagem e desenha no frame.
// Devolve true se o blob for considerado uma laranja aprovada.
static bool processaBlob(cv::Mat& frame, const OVC& blob, int nframe,
                         IVC* imageDilated, IVC* imageMask,
                         std::vector<BlobTrack>& rastreio,
                         std::vector<LaranjaInfo>& laranjas,
                         int& proximoId,
                         float diamMinFrame, float diamMaxFrame)
{
    const int   cx      = blob.xc;
    const int   cy      = blob.yc;
    const int   blobY   = blob.y;
    const float diam_mm = calcDiametroMM(blob.width, blob.height);
    const int   calibre = calcCalibre(diam_mm);

    const bool aprovada = (diam_mm >= DIAM_MIN_MM);
    if (!aprovada) return false;

    // ── CORREÇÃO 1: verificar antecipadamente se este blob já foi contado.
    // Só calcular defeito e categoria se ainda não contado — evita recalcular
    // métricas frame a frame e impede que um blob já contado seja descartado
    // por "Fora" devido a variações na máscara.
    bool jaContado = false;
    for (int j = 0; j < (int)rastreio.size(); j++) {
        float dx = (float)(cx - rastreio[j].cx);
        float dy = (float)(cy - rastreio[j].cy);
        if (sqrtf(dx*dx + dy*dy) < DIST_MAX && rastreio[j].contado) {
            jaContado = true;
            break;
        }
    }
    float       pct_defeito = 0.0f;
    const char* cat         = "?";
    if (!jaContado) {
        pct_defeito = calcDefeito(imageDilated, imageMask, blob);
        cat = calcCategoria(blob.area, blob.perimeter, pct_defeito,
                            diamMinFrame, diamMaxFrame);
        if (strcmp(cat, "Fora") == 0) return false;
    }

    // ── Rastreio: associar ao track mais próximo ──────────────────────

    int   idxProximo = -1;
    float distMin    = 99999.0f;

    for (int j = 0; j < (int)rastreio.size(); j++) {
        float dx   = (float)(cx - rastreio[j].cx);
        float dy   = (float)(cy - rastreio[j].cy);
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < distMin) { distMin = dist; idxProximo = j; }
    }

    LaranjaInfo* infoCongelado = nullptr;

    if (idxProximo >= 0 && distMin < DIST_MAX) {
        BlobTrack& t = rastreio[idxProximo];

        t.cx         = cx;
        t.cy         = cy;
        t.blobY      = blobY;
        t.frameVista = nframe;

        if (!t.contado) {
            t.diametro_mm = diam_mm;
            t.calibre     = calibre;
            strncpy(t.categoria, cat, sizeof(t.categoria) - 1);
            t.area        = blob.area;
            t.perimetro   = blob.perimeter;
            t.aprovada    = aprovada;
            t.pct_defeito = pct_defeito;

            if (blobY >= LINHA_CONTAGEM) {
                t.contado   = true;
                t.laranjaId = proximoId;   // ── CORREÇÃO 2: guardar o id no track
                LaranjaInfo info = criarInfo(proximoId++, diam_mm, calibre,
                                             cat, blob, cx, cy, aprovada, pct_defeito);
                laranjas.push_back(info);
                logContagem(info);
                infoCongelado = &laranjas.back();
            }
        }
        else {
            // ── CORREÇÃO 2: já contada — localizar registo pelo id, não por distância.
            // A busca por distância falhava porque t.cx/t.cy já tinham sido atualizados
            // antes deste bloco, tornando a comparação inválida.
            for (auto& li : laranjas) {
                if (li.id == t.laranjaId) {
                    li.cx = cx;
                    li.cy = cy;
                    infoCongelado = &li;
                    break;
                }
            }
        }
    }
    else {
        // Novo track
        BlobTrack nova;
        nova.cx          = cx;
        nova.cy          = cy;
        nova.blobY       = blobY;
        nova.frameVista  = nframe;
        nova.contado     = (blobY >= LINHA_CONTAGEM);
        nova.laranjaId   = nova.contado ? proximoId : -1;  // ── CORREÇÃO 2
        nova.diametro_mm = diam_mm;
        nova.calibre     = calibre;
        strncpy(nova.categoria, cat, sizeof(nova.categoria) - 1);
        nova.area        = blob.area;
        nova.perimetro   = blob.perimeter;
        nova.aprovada    = aprovada;
        nova.pct_defeito = pct_defeito;
        rastreio.push_back(nova);

        if (nova.contado) {
            LaranjaInfo info = criarInfo(proximoId++, diam_mm, calibre,
                                         cat, blob, cx, cy, aprovada, pct_defeito);
            laranjas.push_back(info);
            logContagem(info);
            infoCongelado = &laranjas.back();
        }
    }

    // ── Valores a usar para o desenho ────────────────────────────────
    // Se já contada: usar valores congelados; caso contrário, os atuais.

    float       draw_diam    = infoCongelado ? infoCongelado->diametro_mm : diam_mm;
    int         draw_calibre = infoCongelado ? infoCongelado->calibre     : calibre;
    const char* draw_cat     = infoCongelado ? infoCongelado->categoria   : cat;
    int         draw_area    = infoCongelado ? infoCongelado->area        : blob.area;
    int         draw_peri    = infoCongelado ? infoCongelado->perimetro   : blob.perimeter;
    bool        draw_aprov   = infoCongelado ? infoCongelado->aprovada    : aprovada;
    float       draw_def     = infoCongelado ? infoCongelado->pct_defeito : pct_defeito;
    bool        draw_defeito = (draw_def > DEFEITO_THRESHOLD);

    // Bounding-box real do blob atual (para acompanhar o movimento)
    int draw_bx = blob.x;
    int draw_by = blob.y;
    int draw_bw = blob.width;
    int draw_bh = blob.height;

    // ── Desenho ───────────────────────────────────────────────────────

    cv::Scalar cor = corCategoria(draw_cat);
    char buf[128];

    // Bounding-box — cv::rectangle (função OpenCV nº 2)
    cv::rectangle(frame,
        cv::Point(draw_bx,            draw_by),
        cv::Point(draw_bx + draw_bw,  draw_by + draw_bh),
        cor, draw_defeito ? 1 : 2);

    // Crosshair (+) com cv::line (função OpenCV nº 3)
    cv::line(frame, cv::Point(cx - 8, cy), cv::Point(cx + 8, cy), cv::Scalar(255,255,255), 1);
    cv::line(frame, cv::Point(cx, cy - 8), cv::Point(cx, cy + 8), cv::Scalar(255,255,255), 1);

    int textY = (draw_by - 10 < 60) ? draw_by + draw_bh + 16 : draw_by - 10;

    // Linha 1: área e perímetro
    float area_mm2 = (float)draw_area * MM_PER_PIXEL * MM_PER_PIXEL;
    float peri_mm  = (float)draw_peri * MM_PER_PIXEL;
    sprintf(buf, "A:%.2fcm2 P:%.2fmm", area_mm2 / 100.0f, peri_mm);
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY),     0.45, cv::Scalar(255,255,255));

    // Linha 2: diâmetro e calibre
    if (draw_calibre >= 0) sprintf(buf, "D:%.1fmm CAL:%d",    draw_diam, draw_calibre);
    else                   sprintf(buf, "D:%.1fmm CAL:<min",  draw_diam);
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY+16),  0.45, cor);

    // Linha 3: categoria
    sprintf(buf, "CAT:%s", draw_cat);
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY+32),  0.45, cor);

    // Linha 4: aprovação e defeito
    sprintf(buf, "Aprov:%s Def:%d%%",
            draw_aprov ? "SIM" : "NAO",
            (int)(draw_def * 100));
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY+48), 0.45,
                    draw_aprov ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255));

    // Linha 5: ID (só se já contada)
    if (infoCongelado) {
        sprintf(buf, "#%d", infoCongelado->id);
        putTextOutlined(frame, buf, cv::Point(draw_bx, textY+64), 0.55,
                        cv::Scalar(0,255,255));
    }

    return true;
}

// ============================================================
//  HUD geral
// ============================================================

static void desenhaHUD(cv::Mat& frame, int nframe, int ntotalframes,
                       int nlaranjas, int nlaranjaframe, int width, int height)
{
    // Linha de contagem com cv::line (função OpenCV nº 3)
    cv::line(frame, cv::Point(0, LINHA_CONTAGEM), cv::Point(width, LINHA_CONTAGEM),
             cv::Scalar(0, 0, 255), 2);

    char buf[128];

    sprintf(buf, "LINHA CONTAGEM");
    putTextOutlined(frame, buf,
                    cv::Point(width / 2 - 80, LINHA_CONTAGEM - 5),
                    0.5, cv::Scalar(0, 0, 255));

    sprintf(buf, "TOTAL CONTADAS: %d", nlaranjas);
    putTextOutlined(frame, buf,
                    cv::Point(20, LINHA_CONTAGEM + 25), 0.7, cv::Scalar(0, 200, 0));

    sprintf(buf, "NO FRAME: %d", nlaranjaframe);
    putTextOutlined(frame, buf,
                    cv::Point(20, LINHA_CONTAGEM + 50), 0.6, cv::Scalar(0, 220, 255));

    sprintf(buf, "FRAME: %d/%d", nframe, ntotalframes);
    putTextOutlined(frame, buf,
                    cv::Point(20, LINHA_CONTAGEM + 75), 0.6, cv::Scalar(255, 255, 255));

    // Legenda de categorias
    const struct { const char* label; cv::Scalar cor; } legenda[] = {
        { "Extra",      cv::Scalar(  0, 255,   0) },
        { "Cat.I",      cv::Scalar(255, 255,   0) },
        { "Cat.II/III", cv::Scalar(  0, 165, 255) },
    };
    for (int k = 0; k < 3; k++) {
        cv::putText(frame, legenda[k].label,
                    cv::Point(10, height - 80 + k * 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, legenda[k].cor, 1);
    }
}

// ============================================================
//  Resumo final no terminal
// ============================================================

static void imprimeResumo(const std::vector<LaranjaInfo>& laranjas)
{
    std::cout << "\n===== RESUMO FINAL =====\n";
    std::cout << "Total de laranjas contadas: " << laranjas.size() << "\n\n";
    std::cout << std::left
              << std::setw( 6) << "ID"
              << std::setw(10) << "Categ."
              << std::setw(12) << "Diam(mm)"
              << std::setw( 8) << "Calibre"
              << std::setw(14) << "Area(cm2)"
              << std::setw(14) << "Perim(mm)"
              << std::setw(10) << "Aprovada"
              << std::setw(10) << "Defeito%"
              << "\n" << std::string(84, '-') << "\n";

    for (const auto& l : laranjas) {
        float a_cm2 = l.area      * MM_PER_PIXEL * MM_PER_PIXEL / 100.0f;
        float p_mm  = l.perimetro * MM_PER_PIXEL;
        std::cout << std::left
                  << std::setw( 6) << l.id
                  << std::setw(10) << l.categoria
                  << std::setw(12) << l.diametro_mm
                  << std::setw( 8) << l.calibre
                  << std::setw(14) << a_cm2
                  << std::setw(14) << p_mm
                  << std::setw(10) << (l.aprovada ? "SIM" : "NAO")
                  << std::setw(10) << (int)(l.pct_defeito * 100)
                  << "\n";
    }
}

// ============================================================
//  Função principal
// ============================================================

extern "C" int processaVideo(const char* videofile)
{
    cv::VideoCapture capture(videofile);
    if (!capture.isOpened()) {
        std::cerr << "Erro ao abrir o ficheiro de video!\n";
        return 1;
    }

    const int width        = (int)capture.get(cv::CAP_PROP_FRAME_WIDTH);
    const int height       = (int)capture.get(cv::CAP_PROP_FRAME_HEIGHT);
    const int ntotalframes = (int)capture.get(cv::CAP_PROP_FRAME_COUNT);

    // Alocar imagens IVC fora do loop (evita realocações por frame)
    IVC* imageBGR     = vc_image_new(width, height, 3, 255);
    IVC* imageHSV     = vc_image_new(width, height, 3, 255);
    IVC* imageMask    = vc_image_new(width, height, 1, 255);
    IVC* imageDilated = vc_image_new(width, height, 1, 255);
    IVC* imageEroded  = vc_image_new(width, height, 1, 255);
    IVC* imageLabels  = vc_image_new(width, height, 1, 255);

    std::vector<LaranjaInfo> laranjas;
    std::vector<BlobTrack>   rastreio;
    int proximoId = 1;

    cv::namedWindow("VC - VIDEO", cv::WINDOW_AUTOSIZE);
    cv::Mat frame;
    int key = 0;

    while (key != 'q') {
        capture.read(frame);
        if (frame.empty()) break;

        const int nframe = (int)capture.get(cv::CAP_PROP_POS_FRAMES);

        // 1) Copiar frame para IVC e converter BGR -> HSV
        memcpy(imageBGR->data, frame.data, width * height * 3);
        vc_bgr_to_hsv(imageBGR, imageHSV);

        // 2) Segmentação: isolar píxeis com cor laranja
        //    H: 5-35 graus, S: 40-100 %, V: 30-100 % (escala GIMP)
        vc_hsv_segmentation(imageHSV, imageMask, 15, 35, 40, 100, 35, 100);
        // 3) Morfologia: fechar buracos (dilatação) e remover ruído (erosão)
        vc_binary_dilate(imageMask,    imageDilated, 7);
        vc_binary_erode (imageDilated, imageEroded,  3);

        // 4) Labeling: identificar e separar blobs
        int  nlabels = 0;
        OVC* blobs   = vc_binary_blob_labelling(imageEroded, imageLabels, &nlabels);

        float diamMinFrame = 99999.0f;
        float diamMaxFrame = 0.0f;
        int   nLaranjaFrame = 0;

        if (blobs != NULL && nlabels > 0) {
            vc_binary_blob_info(imageLabels, blobs, nlabels);

            // Passagem 1: calcular diâmetros mínimo e máximo dos blobs aprovados
            for (int i = 0; i < nlabels; i++) {
                if (blobs[i].area < AREA_MIN) continue;
                float d = calcDiametroMM(blobs[i].width, blobs[i].height);
                if (d >= DIAM_MIN_MM) {
                    if (d < diamMinFrame) diamMinFrame = d;
                    if (d > diamMaxFrame) diamMaxFrame = d;
                }
            }

            // Passagem 2: processar cada blob com os diâmetros já calculados
            for (int i = 0; i < nlabels; i++) {
                if (blobs[i].area < AREA_MIN) continue;
                float d = calcDiametroMM(blobs[i].width, blobs[i].height);
                if (d >= DIAM_MIN_MM) {
                    if (processaBlob(frame, blobs[i], nframe, imageEroded, imageMask,
                                     rastreio, laranjas, proximoId,
                                     diamMinFrame, diamMaxFrame))
                        nLaranjaFrame++;
                }
            }
            free(blobs);
        }

        // 5) HUD geral
        desenhaHUD(frame, nframe, ntotalframes,
                   (int)laranjas.size(), nLaranjaFrame, width, height);

        // 6) Remover tracks ausentes há demasiados frames
        for (int j = (int)rastreio.size() - 1; j >= 0; j--) {
            if (nframe - rastreio[j].frameVista > FRAMES_AUSENTE)
                rastreio.erase(rastreio.begin() + j);
        }

        cv::imshow("VC - VIDEO", frame);
        key = cv::waitKey(1);
    }

    vc_image_free(imageBGR);
    vc_image_free(imageHSV);
    vc_image_free(imageMask);
    vc_image_free(imageDilated);
    vc_image_free(imageEroded);
    vc_image_free(imageLabels);

    cv::destroyWindow("VC - VIDEO");
    capture.release();

    imprimeResumo(laranjas);
    return 0;
}