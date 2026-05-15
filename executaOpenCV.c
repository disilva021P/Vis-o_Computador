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

#define MM_PER_PIXEL      (55.0f / 280.0f)
#define LINHA_CONTAGEM    40
#define AREA_MIN          25000
#define DIST_MAX          150.0f
#define FRAMES_AUSENTE    15
#define DIAM_MIN_MM       53.0f
#define DEFEITO_THRESHOLD 0.10f

struct LaranjaInfo {
    int   id;
    float diametro_mm;
    int   calibre;
    char  categoria[16];
    int   area;
    int   perimetro;
    int   cx, cy;
    bool  aprovada;
    float pct_defeito;
    int   bbox_w, bbox_h;
};

struct BlobTrack {
    int   cx, cy;
    int   blobY;
    int   frameVista;
    bool  contado;
    int   laranjaId;
    float diametro_mm;
    int   calibre;
    char  categoria[16];
    int   area;
    int   perimetro;
    bool  aprovada;
    float pct_defeito;
    int   bbox_w, bbox_h;
};

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

static float calcDiametroMM(int bboxW, int bboxH)
{
    return ((bboxW + bboxH) / 2.0f) * MM_PER_PIXEL;
}

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

static const char* calcCategoria(int area, int perimetro, float pct_defeito,
                                  float diamMin, float diamMax)
{
    if (perimetro == 0) return "?";

    float circ = (4.0f * (float)M_PI * (float)area)
               / ((float)perimetro * (float)perimetro);

    if (circ < 0.50f) return "Fora";

    int pontos = 0;

    if      (circ >= 0.90f) pontos += 0;
    else if (circ >= 0.80f) pontos += 1;
    else                    pontos += 2;

    if      (pct_defeito < 0.05f) pontos += 0;
    else if (pct_defeito < 0.15f) pontos += 1;
    else if (pct_defeito < 0.30f) pontos += 2;
    else                          pontos += 3;

    if (diamMin <= diamMax && diamMax > 0.0f) {
        int cal = calcCalibre(diamMax);
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

static float calcDefeito(IVC* imageDilated, IVC* imageMask, const OVC& blob)
{
    unsigned char* dataDil  = imageDilated->data;
    unsigned char* dataMask = imageMask->data;
    int bpl  = imageDilated->bytesperline;
    int ch   = imageDilated->channels;
    int imgW = imageDilated->width;
    int imgH = imageDilated->height;

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

static void putTextOutlined(cv::Mat& img, const char* txt,
                            cv::Point pos, double scale, cv::Scalar cor)
{
    cv::putText(img, txt, pos, cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0,0,0), 2);
    cv::putText(img, txt, pos, cv::FONT_HERSHEY_SIMPLEX, scale, cor,              1);
}

static cv::Scalar corCategoria(const char* cat)
{
    if (strcmp(cat, "Extra")  == 0) return cv::Scalar(  0, 255,   0);
    if (strcmp(cat, "I")      == 0) return cv::Scalar(255, 255,   0);
    if (strcmp(cat, "II/III") == 0) return cv::Scalar(  0, 165, 255);
    return cv::Scalar(128, 128, 128);
}

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
                t.laranjaId = proximoId;
                LaranjaInfo info = criarInfo(proximoId++, diam_mm, calibre,
                                             cat, blob, cx, cy, aprovada, pct_defeito);
                laranjas.push_back(info);
                logContagem(info);
                infoCongelado = &laranjas.back();
            }
        }
        else {
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
        BlobTrack nova;
        nova.cx          = cx;
        nova.cy          = cy;
        nova.blobY       = blobY;
        nova.frameVista  = nframe;
        nova.contado     = (blobY >= LINHA_CONTAGEM);
        nova.laranjaId   = nova.contado ? proximoId : -1;
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

    float       draw_diam    = infoCongelado ? infoCongelado->diametro_mm : diam_mm;
    int         draw_calibre = infoCongelado ? infoCongelado->calibre     : calibre;
    const char* draw_cat     = infoCongelado ? infoCongelado->categoria   : cat;
    int         draw_area    = infoCongelado ? infoCongelado->area        : blob.area;
    int         draw_peri    = infoCongelado ? infoCongelado->perimetro   : blob.perimeter;
    bool        draw_aprov   = infoCongelado ? infoCongelado->aprovada    : aprovada;
    float       draw_def     = infoCongelado ? infoCongelado->pct_defeito : pct_defeito;
    bool        draw_defeito = (draw_def > DEFEITO_THRESHOLD);

    int draw_bx = blob.x;
    int draw_by = blob.y;
    int draw_bw = blob.width;
    int draw_bh = blob.height;

    cv::Scalar cor = corCategoria(draw_cat);
    char buf[128];

    cv::rectangle(frame,
        cv::Point(draw_bx,            draw_by),
        cv::Point(draw_bx + draw_bw,  draw_by + draw_bh),
        cor, draw_defeito ? 1 : 2);

    cv::line(frame, cv::Point(cx - 8, cy), cv::Point(cx + 8, cy), cv::Scalar(255,255,255), 1);
    cv::line(frame, cv::Point(cx, cy - 8), cv::Point(cx, cy + 8), cv::Scalar(255,255,255), 1);

    int textY = (draw_by - 10 < 60) ? draw_by + draw_bh + 16 : draw_by - 10;

    float area_mm2 = (float)draw_area * MM_PER_PIXEL * MM_PER_PIXEL;
    float peri_mm  = (float)draw_peri * MM_PER_PIXEL;
    sprintf(buf, "A:%.2fcm2 P:%.2fmm", area_mm2 / 100.0f, peri_mm);
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY),     0.45, cv::Scalar(255,255,255));

    if (draw_calibre >= 0) sprintf(buf, "D:%.1fmm CAL:%d",    draw_diam, draw_calibre);
    else                   sprintf(buf, "D:%.1fmm CAL:<min",  draw_diam);
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY+16),  0.45, cor);

    sprintf(buf, "CAT:%s", draw_cat);
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY+32),  0.45, cor);

    sprintf(buf, "Aprov:%s Def:%d%%",
            draw_aprov ? "SIM" : "NAO",
            (int)(draw_def * 100));
    putTextOutlined(frame, buf, cv::Point(draw_bx, textY+48), 0.45,
                    draw_aprov ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255));

    if (infoCongelado) {
        sprintf(buf, "#%d", infoCongelado->id);
        putTextOutlined(frame, buf, cv::Point(draw_bx, textY+64), 0.55,
                        cv::Scalar(0,255,255));
    }

    return true;
}

static void desenhaHUD(cv::Mat& frame, int nframe, int ntotalframes,
                       int nlaranjas, int nlaranjaframe, int width, int height)
{
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

        memcpy(imageBGR->data, frame.data, width * height * 3);
        vc_bgr_to_hsv(imageBGR, imageHSV);

        vc_hsv_segmentation(imageHSV, imageMask, 5, 35, 40, 100, 30, 100);

        vc_binary_dilate(imageMask,    imageDilated, 7);
        vc_binary_erode (imageDilated, imageEroded,  7);

        int  nlabels = 0;
        OVC* blobs   = vc_binary_blob_labelling(imageEroded, imageLabels, &nlabels);

        float diamMinFrame = 99999.0f;
        float diamMaxFrame = 0.0f;
        int   nLaranjaFrame = 0;

        if (blobs != NULL && nlabels > 0) {
            vc_binary_blob_info(imageLabels, blobs, nlabels);

            for (int i = 0; i < nlabels; i++) {
                if (blobs[i].area < AREA_MIN) continue;
                float d = calcDiametroMM(blobs[i].width, blobs[i].height);
                if (d >= DIAM_MIN_MM) {
                    if (d < diamMinFrame) diamMinFrame = d;
                    if (d > diamMaxFrame) diamMaxFrame = d;
                }
            }

            for (int i = 0; i < nlabels; i++) {
                if (blobs[i].area < AREA_MIN) continue;
                float d = calcDiametroMM(blobs[i].width, blobs[i].height);
                if (d >= DIAM_MIN_MM) {
                    if (processaBlob(frame, blobs[i], nframe, imageDilated, imageMask,
                                     rastreio, laranjas, proximoId,
                                     diamMinFrame, diamMaxFrame))
                        nLaranjaFrame++;
                }
            }
            free(blobs);
        }

        desenhaHUD(frame, nframe, ntotalframes,
                   (int)laranjas.size(), nLaranjaFrame, width, height);

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