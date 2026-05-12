// ============================================================
//  executaOpenCV.c
//  Contagem e classificação de laranjas em vídeo.
//
//  Pipeline por frame:
//    1) BGR -> HSV
//    2) Segmentação HSV (isola pixels laranja)
//    3) Dilatação + Erosão (fecha buracos e remove ruído)
//    4) Labeling de blobs
//    5) Para cada blob válido:
//         - Calcula diâmetro, calibre, categoria
//         - Deteta defeitos (pixels não-laranja dentro do blob)
//         - Verifica aprovação (diâmetro >= 53 mm)
//         - Rastreia entre frames e conta ao cruzar a linha
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

// ------------------------------------------------------------
//  Constantes globais
// ------------------------------------------------------------

// Fator de conversão píxeis -> milímetros (280 px = 55 mm)
#define MM_PER_PIXEL    (55.0f / 280.0f)

// Ordenada (em píxeis) da linha de contagem.
// Uma laranja é contada quando o topo da sua bounding-box
// ultrapassa esta linha pela primeira vez.
#define LINHA_CONTAGEM  40

// Área mínima de um blob para não ser considerado ruído
#define AREA_MIN        3000

// Distância máxima (em píxeis) entre centros para associar
// um blob detetado a um track existente
#define DIST_MAX        150.0f

// Número de frames consecutivas sem deteção para remover um track
#define FRAMES_AUSENTE  15

// Diâmetro mínimo para aprovação comercial (Regulamento CEE 379/71)
#define DIAM_MIN_MM     53.0f

// Limiar de defeito: percentagem de píxeis não-laranja dentro
// da bounding-box acima da qual a laranja é considerada com defeito
#define DEFEITO_THRESHOLD 0.50f

// ------------------------------------------------------------
//  Estruturas de dados
// ------------------------------------------------------------

// Registo permanente de uma laranja já contada (após cruzar a linha)
struct LaranjaInfo {
    int   id;               // identificador sequencial
    float diametro_mm;      // diâmetro estimado em mm
    int   calibre;          // calibre (0-13) ou -1 se abaixo do mínimo
    char  categoria[16];    // "Extra", "I", "II" ou "III"
    int   area;             // área do blob em píxeis
    int   perimetro;        // perímetro do blob em píxeis
    int   cx, cy;           // centro de gravidade
    bool  aprovada;         // true se diâmetro >= DIAM_MIN_MM
    float pct_defeito;      // percentagem de píxeis com defeito (0.0 - 1.0)
};

// Estado de rastreio de um blob entre frames consecutivos.
// Mantido enquanto a laranja está visível mas ainda não foi contada.
struct BlobTrack {
    int   cx, cy;           // centro de gravidade atual
    int   blobY;            // topo da bounding-box atual
    int   frameVista;       // última frame em que foi detetado
    bool  contado;          // true se já cruzou a linha de contagem
    float diametro_mm;
    int   calibre;
    char  categoria[16];
    int   area;
    int   perimetro;
    bool  aprovada;
    float pct_defeito;
};

// ------------------------------------------------------------
//  Funções de classificação (Regulamento CEE 379/71)
// ------------------------------------------------------------

// Calcula o diâmetro estimado em mm a partir das dimensões
// da bounding-box, usando a média entre largura e altura.
static float calcDiametroMM(int bboxW, int bboxH)
{
    return ((bboxW + bboxH) / 2.0f) * MM_PER_PIXEL;
}

// Devolve o calibre (0-13) conforme a tabela do Regulamento CEE 379/71,
// secção III — Calibre, escala de calibre para laranjas.
// Devolve -1 se o diâmetro for inferior ao mínimo de 53 mm.
static int calcCalibre(float diametro_mm)
{
    if (diametro_mm >= 100.0f) return 0;
    if (diametro_mm >=  87.0f) return 1;
    if (diametro_mm >=  84.0f) return 2;
    if (diametro_mm >=  81.0f) return 3;
    if (diametro_mm >=  77.0f) return 4;
    if (diametro_mm >=  73.0f) return 5;
    if (diametro_mm >=  70.0f) return 6;
    if (diametro_mm >=  67.0f) return 7;
    if (diametro_mm >=  64.0f) return 8;
    if (diametro_mm >=  62.0f) return 9;
    if (diametro_mm >=  60.0f) return 10;
    if (diametro_mm >=  58.0f) return 11;
    if (diametro_mm >=  56.0f) return 12;
    if (diametro_mm >=  53.0f) return 13;
    return -1; // abaixo do calibre mínimo
}

// Calcula a categoria com base na circularidade do blob.
// Fórmula: circ = (4 * PI * area) / (perimetro^2)
//   circ >= 0.90 -> Extra
//   circ >= 0.80 -> I
//   circ >= 0.65 -> II
//   circ >= 0.50 -> III
//   circ <  0.50 -> "Fora" (não comercializável; ignorado no pipeline)
// Regulamento CEE 379/71, secção II.D — Classificação.
static const char* calcCategoria(int area, int perimetro)
{
    if (perimetro == 0) return "?";

    float circ = (4.0f * (float)M_PI * (float)area)
               / ((float)perimetro * (float)perimetro);

    if (circ >= 0.90f) return "Extra";
    if (circ >= 0.80f) return "I";
    if (circ >= 0.65f) return "II";
    if (circ >= 0.50f) return "III";
    return "Fora";
}

// ------------------------------------------------------------
//  Deteção de defeitos
// ------------------------------------------------------------

// Estima a percentagem de defeito de uma laranja analisando
// os píxeis dentro da sua bounding-box na imagem HSV.
//
// Estratégia: um píxel é considerado "com defeito" se NÃO
// corresponder à gama de cor laranja (manchas escuras, zonas
// verdes, etc.). Conta-se a proporção desses píxeis em relação
// à área total da bounding-box.
//
// Parâmetros HSV de cor laranja (em escala GIMP):
//   H: 5-35, S: 40-100, V: 30-100
// Convertidos para escala 0-255 usada no IVC.
static float calcDefeito(IVC* imageHSV, const OVC& blob)
{
    // Limites HSV da cor laranja (convertidos de GIMP para 0-255)
    const int hmin = (int)(( 5.0f / 360.0f) * 255);
    const int hmax = (int)((35.0f / 360.0f) * 255);
    const int smin = (int)((40.0f / 100.0f) * 255);
    const int smax = 255;
    const int vmin = (int)((30.0f / 100.0f) * 255);
    const int vmax = 255;

    unsigned char* data       = imageHSV->data;
    int            bpl        = imageHSV->bytesperline; // bytes por linha
    int            ch         = imageHSV->channels;
    int            imgW       = imageHSV->width;
    int            imgH       = imageHSV->height;

    // Limitar a bounding-box aos limites da imagem
    int x0 = blob.x;
    int y0 = blob.y;
    int x1 = blob.x + blob.width;
    int y1 = blob.y + blob.height;
    if (x0 < 0)    x0 = 0;
    if (y0 < 0)    y0 = 0;
    if (x1 > imgW) x1 = imgW;
    if (y1 > imgH) y1 = imgH;

    int total   = 0; // total de píxeis analisados
    int defeito = 0; // píxeis fora da gama laranja

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            long int pos = y * bpl + x * ch;
            int h = data[pos];
            int s = data[pos + 1];
            int v = data[pos + 2];

            total++;

            // Se o píxel não for laranja, conta como defeito
            if (!(h >= hmin && h <= hmax &&
                  s >= smin && s <= smax &&
                  v >= vmin && v <= vmax))
            {
                defeito++;
            }
        }
    }

    if (total == 0) return 0.0f;
    return (float)defeito / (float)total;
}

// ------------------------------------------------------------
//  Funções auxiliares de rastreio e desenho
// ------------------------------------------------------------

// Preenche um LaranjaInfo com todos os dados calculados para o blob.
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
    return info;
}

// Imprime no terminal a informação de uma laranja no momento da contagem.
static void logContagem(const LaranjaInfo& info)
{
    std::cout << "[CONTAGEM] Laranja #" << info.id
              << "  Cat:"      << info.categoria
              << "  D:"        << info.diametro_mm << "mm"
              << "  Cal:"      << info.calibre
              << "  A:"        << info.area
              << "  P:"        << info.perimetro
              << "  Aprov:"    << (info.aprovada ? "SIM" : "NAO")
              << "  Def:"      << (int)(info.pct_defeito * 100) << "%"
              << "\n";
}

// Devolve a cor BGR associada a cada categoria (para o desenho no frame).
static cv::Scalar corCategoria(const char* cat)
{
    if (strcmp(cat, "Extra") == 0) return cv::Scalar(  0, 255,   0); // verde
    if (strcmp(cat, "I")     == 0) return cv::Scalar(255, 255,   0); // ciano
    if (strcmp(cat, "II")    == 0) return cv::Scalar(  0, 165, 255); // laranja
    if (strcmp(cat, "III")   == 0) return cv::Scalar(  0,   0, 255); // vermelho
    return cv::Scalar(128, 128, 128);
}

// Desenha texto com contorno preto para garantir legibilidade
// sobre qualquer fundo do vídeo.
static void putTextOutlined(cv::Mat& img, const char* txt,
                            cv::Point pos, double scale, cv::Scalar cor)
{
    cv::putText(img, txt, pos, cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0,0,0), 2);
    cv::putText(img, txt, pos, cv::FONT_HERSHEY_SIMPLEX, scale, cor,              1);
}

// ------------------------------------------------------------
//  Processamento de cada blob detetado num frame
// ------------------------------------------------------------

// Processa um blob individual: calcula as suas métricas,
// associa-o a um track existente (ou cria um novo), conta-o
// ao cruzar a linha de contagem, e desenha a informação no frame.
static bool processaBlob(cv::Mat& frame, const OVC& blob, int nframe,
                         IVC* imageHSV,
                         std::vector<BlobTrack>& rastreio,
                         std::vector<LaranjaInfo>& laranjas,
                         int& proximoId)
{
    const int   cx      = blob.xc;
    const int   cy      = blob.yc;
    const int   blobY   = blob.y;
    const float diam_mm = calcDiametroMM(blob.width, blob.height);
    const int   calibre = calcCalibre(diam_mm);
    const char* cat     = calcCategoria(blob.area, blob.perimeter);

    // Blobs com circularidade muito baixa não são laranjas válidas —
    // ignorar completamente (sem desenho nem rastreio)
    if (strcmp(cat, "Fora") == 0) return false;

    const bool aprovada = (diam_mm >= DIAM_MIN_MM);
    if (!aprovada) return false;

    // Deteção de defeitos: analisa a percentagem de píxeis
    // não-laranja dentro da bounding-box na imagem HSV
    const float pct_defeito = calcDefeito(imageHSV, blob);
    const bool  tem_defeito = (pct_defeito > DEFEITO_THRESHOLD);

    // ── Rastreio: associar ao track mais próximo ─────────────
    // Percorre todos os tracks ativos e encontra o mais próximo
    // com base na distância euclidiana entre centros de gravidade.
    int   idxProximo = -1;
    float distMin    = 99999.0f;

    for (int j = 0; j < (int)rastreio.size(); j++) {
        float dx   = (float)(cx - rastreio[j].cx);
        float dy   = (float)(cy - rastreio[j].cy);
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < distMin) { distMin = dist; idxProximo = j; }
    }

    if (idxProximo >= 0 && distMin < DIST_MAX) {
        // Track existente encontrado — atualizar com os dados do frame atual
        BlobTrack& t  = rastreio[idxProximo];
        t.cx          = cx;
        t.cy          = cy;
        t.blobY       = blobY;
        t.frameVista  = nframe;
        t.diametro_mm = diam_mm;
        t.calibre     = calibre;
        strncpy(t.categoria, cat, sizeof(t.categoria) - 1);
        t.area        = blob.area;
        t.perimetro   = blob.perimeter;
        t.aprovada    = aprovada;
        t.pct_defeito = pct_defeito;

        // Contagem: o topo da bounding-box cruzou a linha pela primeira vez
        if (!t.contado && blobY >= LINHA_CONTAGEM) {
            t.contado = true;
            LaranjaInfo info = criarInfo(proximoId++, diam_mm, calibre,
                                         cat, blob, cx, cy, aprovada, pct_defeito);
            laranjas.push_back(info);
            logContagem(info);
        }
    }
    else {
        // Nenhum track próximo — este é um blob novo, criar track
        BlobTrack nova;
        nova.cx          = cx;
        nova.cy          = cy;
        nova.blobY       = blobY;
        nova.frameVista  = nframe;
        // Se o blob já entrou abaixo da linha (vídeo começou a meio),
        // marca como contado imediatamente para evitar duplicados
        nova.contado     = (blobY >= LINHA_CONTAGEM);
        nova.diametro_mm = diam_mm;
        nova.calibre     = calibre;
        strncpy(nova.categoria, cat, sizeof(nova.categoria) - 1);
        nova.area        = blob.area;
        nova.perimetro   = blob.perimeter;
        nova.aprovada    = aprovada;
        nova.pct_defeito = pct_defeito;
        rastreio.push_back(nova);

        // Se entrou já abaixo da linha, contar imediatamente
        if (nova.contado) {
            LaranjaInfo info = criarInfo(proximoId++, diam_mm, calibre,
                                         cat, blob, cx, cy, aprovada, pct_defeito);
            laranjas.push_back(info);
            logContagem(info);
        }
    }

    // ── Desenho sobre o frame ────────────────────────────────
    cv::Scalar cor = corCategoria(cat);
    char buf[128];

    // Bounding-box a tracejado se tiver defeito, sólido se não tiver
    cv::rectangle(frame,
        cv::Point(blob.x, blob.y),
        cv::Point(blob.x + blob.width, blob.y + blob.height),
        cor, tem_defeito ? 1 : 2);

    // Crosshair no centro de gravidade
    cv::line(frame, cv::Point(cx-8, cy), cv::Point(cx+8, cy), cv::Scalar(255,255,255), 2);
    cv::line(frame, cv::Point(cx, cy-8), cv::Point(cx, cy+8), cv::Scalar(255,255,255), 2);

    // Posição do bloco de texto: acima da bbox se houver espaço,
    // abaixo caso contrário (evita sair do topo do frame)
    int textY = (blob.y - 10 < 60) ? blob.y + blob.height + 16 : blob.y - 10;

    // Linha 1: área e perímetro
    sprintf(buf, "A:%d P:%d", blob.area, blob.perimeter);
    putTextOutlined(frame, buf, cv::Point(blob.x, textY),    0.45, cv::Scalar(255,255,255));

    // Linha 2: diâmetro e calibre
    if (calibre >= 0) sprintf(buf, "D:%.1fmm CAL:%d",   diam_mm, calibre);
    else              sprintf(buf, "D:%.1fmm CAL:<min", diam_mm);
    putTextOutlined(frame, buf, cv::Point(blob.x, textY+16), 0.45, cor);

    // Linha 3: categoria
    sprintf(buf, "CAT:%s", cat);
    putTextOutlined(frame, buf, cv::Point(blob.x, textY+32), 0.45, cor);

    // Linha 4: aprovação e defeito
    sprintf(buf, "Aprov:%s Def:%d%%",
            aprovada ? "SIM" : "NAO",
            (int)(pct_defeito * 100));
    putTextOutlined(frame, buf, cv::Point(blob.x, textY+48), 0.45,
                    aprovada ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255));

    // Linha 5: ID da laranja (apenas se já foi contada)
    for (const auto& li : laranjas) {
        if (abs(li.cx - cx) < (int)DIST_MAX && abs(li.cy - cy) < (int)DIST_MAX) {
            sprintf(buf, "#%d", li.id);
            cv::putText(frame, buf, cv::Point(blob.x, textY+64),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0,0,0),    3);
            cv::putText(frame, buf, cv::Point(blob.x, textY+64),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0,255,255), 1);
            break;
        }
    }
    return true;
}

// ------------------------------------------------------------
//  HUD — informação geral sobre o frame
// ------------------------------------------------------------

static void desenhaHUD(cv::Mat& frame, int nframe, int ntotalframes,
                       int nlaranjas, int nlaranjaframe, int width, int height)
{
    // Linha de contagem (referência visual horizontal)
    cv::line(frame,
             cv::Point(0, LINHA_CONTAGEM),
             cv::Point(width, LINHA_CONTAGEM),
             cv::Scalar(0, 0, 255), 2);
    cv::putText(frame, "LINHA CONTAGEM",
                cv::Point(width / 2 - 80, LINHA_CONTAGEM - 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);

    // Total de laranjas contadas e número do frame atual
    std::string str = "TOTAL CONTADAS: " + std::to_string(nlaranjas);
    putTextOutlined(frame, str.c_str(), cv::Point(20, LINHA_CONTAGEM + 25),
                    0.7, cv::Scalar(0, 200, 0));

    str = "NO FRAME: " + std::to_string(nlaranjaframe);
    putTextOutlined(frame, str.c_str(), cv::Point(20, LINHA_CONTAGEM + 50),
                    0.6, cv::Scalar(0, 220, 255));

    str = "FRAME: " + std::to_string(nframe) + "/" + std::to_string(ntotalframes);
    putTextOutlined(frame, str.c_str(), cv::Point(20, LINHA_CONTAGEM + 75),
                    0.6, cv::Scalar(255, 255, 255));

    // Legenda de categorias no canto inferior esquerdo
    const struct { const char* label; cv::Scalar cor; } legenda[] = {
        { "Extra",   cv::Scalar(  0, 255,   0) },
        { "Cat.I",   cv::Scalar(255, 255,   0) },
        { "Cat.II",  cv::Scalar(  0, 165, 255) },
        { "Cat.III", cv::Scalar(  0,   0, 255) },
    };
    for (int k = 0; k < 4; k++) {
        cv::putText(frame, legenda[k].label,
                    cv::Point(10, height - 80 + k * 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, legenda[k].cor, 1);
    }
}

// ------------------------------------------------------------
//  Resumo final no terminal
// ------------------------------------------------------------

static void imprimeResumo(const std::vector<LaranjaInfo>& laranjas)
{
    std::cout << "\n===== RESUMO FINAL =====\n";
    std::cout << "Total de laranjas contadas: " << laranjas.size() << "\n\n";
    std::cout << std::left
              << std::setw(6)  << "ID"
              << std::setw(10) << "Categ."
              << std::setw(12) << "Diam(mm)"
              << std::setw(8)  << "Calibre"
              << std::setw(10) << "Area"
              << std::setw(12) << "Perimetro"
              << std::setw(10) << "Aprovada"
              << std::setw(10) << "Defeito%"
              << "\n"
              << std::string(78, '-') << "\n";

    for (const auto& l : laranjas) {
        std::cout << std::left
                  << std::setw(6)  << l.id
                  << std::setw(10) << l.categoria
                  << std::setw(12) << l.diametro_mm
                  << std::setw(8)  << l.calibre
                  << std::setw(10) << l.area
                  << std::setw(12) << l.perimetro
                  << std::setw(10) << (l.aprovada ? "SIM" : "NAO")
                  << std::setw(10) << (int)(l.pct_defeito * 100)
                  << "\n";
    }
}

// ------------------------------------------------------------
//  Função principal
// ------------------------------------------------------------

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

    // Alocar imagens IVC uma única vez fora do loop de frames.
    // Evita malloc/free por frame, o que melhora a performance.
    IVC* imageBGR     = vc_image_new(width, height, 3, 255); // frame original em BGR
    IVC* imageHSV     = vc_image_new(width, height, 3, 255); // frame convertido para HSV
    IVC* imageMask    = vc_image_new(width, height, 1, 255); // máscara binária (segmentação)
    IVC* imageDilated = vc_image_new(width, height, 1, 255); // após dilatação
    IVC* imageEroded  = vc_image_new(width, height, 1, 255); // após erosão
    IVC* imageLabels  = vc_image_new(width, height, 1, 255); // imagem com labels dos blobs

    std::vector<LaranjaInfo> laranjas;  // laranjas já contadas (registo permanente)
    std::vector<BlobTrack>   rastreio;  // blobs ativos ainda não contados
    int proximoId = 1;                  // ID sequencial para a próxima laranja contada

    cv::namedWindow("VC - VIDEO", cv::WINDOW_AUTOSIZE);
    cv::Mat frame;
    int key = 0;

    while (key != 'q') {
        capture.read(frame);
        if (frame.empty()) break; // fim do vídeo

        const int nframe = (int)capture.get(cv::CAP_PROP_POS_FRAMES);

        // 1) Copiar frame para IVC e converter BGR -> HSV
        memcpy(imageBGR->data, frame.data, width * height * 3);
        vc_bgr_to_hsv(imageBGR, imageHSV);

        // 2) Segmentação: isolar píxeis com cor laranja
        //    H: 5-35 graus, S: 40-100%, V: 30-100% (escala GIMP)
        vc_hsv_segmentation(imageHSV, imageMask, 5, 35, 40, 100, 30, 100);

        // 3) Morfologia: fechar buracos (dilatar) e remover ruído (erosão)
        vc_binary_dilate(imageMask, imageDilated, 7);
        vc_binary_erode(imageDilated, imageEroded, 7);

        // 4) Labeling: identificar e separar os blobs na imagem bináriac
        int  nlabels = 0;
        OVC* blobs   = vc_binary_blob_labelling(imageEroded, imageLabels, &nlabels);

        if (blobs != NULL && nlabels > 0) {
            // Calcular área, perímetro, centro e bounding-box de cada blob
            vc_binary_blob_info(imageLabels, blobs, nlabels);

            int nLaranjaFrame = 0; // laranjas aprovadas visíveis neste frame

            for (int i = 0; i < nlabels; i++) {
                if (blobs[i].area < AREA_MIN) continue;

                if (processaBlob(frame, blobs[i], nframe, imageHSV,
                                 rastreio, laranjas, proximoId))
                    nLaranjaFrame++;
            }
            free(blobs);

            // 6) Desenhar HUD e apresentar frame
            desenhaHUD(frame, nframe, ntotalframes, (int)laranjas.size(), nLaranjaFrame, width, height);
        }
        else {
            desenhaHUD(frame, nframe, ntotalframes, (int)laranjas.size(), 0, width, height);
        }

        // 5) Limpar tracks que não foram detetados há demasiadas frames
        //    (a laranja saiu do campo de visão)
        for (int j = (int)rastreio.size() - 1; j >= 0; j--) {
            if (nframe - rastreio[j].frameVista > FRAMES_AUSENTE)
                rastreio.erase(rastreio.begin() + j);
        }

        cv::imshow("VC - VIDEO", frame);
        key = cv::waitKey(1);
    }

    // Libertar memória das imagens IVC
    vc_image_free(imageBGR);
    vc_image_free(imageHSV);
    vc_image_free(imageMask);
    vc_image_free(imageDilated);
    vc_image_free(imageEroded);
    vc_image_free(imageLabels);

    cv::destroyWindow("VC - VIDEO");
    capture.release();

    // Imprimir tabela resumo no terminal
    imprimeResumo(laranjas);
    return 0;
}