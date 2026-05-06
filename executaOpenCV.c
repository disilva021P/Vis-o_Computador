#include <iostream>
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

#define MM_PER_PIXEL (55.0f / 280.0f)

// =============================================================
// Linha de contagem — a laranja é contada quando o topo da
// bounding-box (blob.y) cruza esta ordenada pela 1.ª vez
// =============================================================
#define LINHA_CONTAGEM 40   // pixels desde o topo do frame

// =============================================================
// Registo de uma laranja já contada
// =============================================================
struct LaranjaInfo {
    int    id;
    float  diametro_mm;
    int    calibre;
    char   categoria[16];
    int    area;
    int    perimetro;
    int    cx, cy;
};

// =============================================================
// Estado de rastreio de um blob entre frames
// (antes de ser contado como laranja completa)
// =============================================================
struct BlobTrack {
    int  cx, cy;          // centro atual
    int  blobY;           // topo da bounding-box (blob.y)
    int  frameVista;      // última frame em que foi detetado
    bool contado;         // já cruzou a linha de contagem?
    // dados do último blob visto (para guardar no momento da contagem)
    float diametro_mm;
    int   calibre;
    char  categoria[16];
    int   area;
    int   perimetro;
};

// =============================================================
// Funções auxiliares
// =============================================================
float calcDiametroMM(int bboxW, int bboxH)
{
    return ((bboxW + bboxH) / 2.0f) * MM_PER_PIXEL;
}

int calcCalibre(float diametro_mm)
{
    if (diametro_mm >= 100.0f) return 0;
    if (diametro_mm >= 87.0f)  return 1;
    if (diametro_mm >= 84.0f)  return 2;
    if (diametro_mm >= 81.0f)  return 3;
    if (diametro_mm >= 77.0f)  return 4;
    if (diametro_mm >= 73.0f)  return 5;
    if (diametro_mm >= 70.0f)  return 6;
    if (diametro_mm >= 67.0f)  return 7;
    if (diametro_mm >= 64.0f)  return 8;
    if (diametro_mm >= 62.0f)  return 9;
    if (diametro_mm >= 60.0f)  return 10;
    if (diametro_mm >= 58.0f)  return 11;
    if (diametro_mm >= 56.0f)  return 12;
    if (diametro_mm >= 53.0f)  return 13;
    return -1;
}

const char* calcCategoria(int area, int perimetro)
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

// =============================================================
// Função principal
// =============================================================
extern "C" int processaVideo(const char* videofile)
{
    cv::VideoCapture capture;

    struct {
        int width, height;
        int ntotalframes;
        int fps;
        int nframe;
    } video;

    std::string str;
    int key         = 0;
    int proximoId   = 1;          // ID sequencial das laranjas contadas

    // Lista de laranjas já contadas (registo permanente)
    std::vector<LaranjaInfo> laranjas;

    // Rastreio de blobs ativos (ainda não contados ou em trânsito)
    std::vector<BlobTrack> rastreio;

    // Distância máxima em píxeis para associar blob a track existente
    const float DIST_MAX      = 150.0f;
    // Frames sem aparecer para remover do rastreio
    const int   FRAMES_AUSENTE = 15;

    // ==========================================================
    capture.open(videofile);
    if (!capture.isOpened()) {
        std::cerr << "Erro ao abrir o ficheiro de video!\n";
        return 1;
    }

    video.ntotalframes = (int)capture.get(cv::CAP_PROP_FRAME_COUNT);
    video.fps          = (int)capture.get(cv::CAP_PROP_FPS);
    video.width        = (int)capture.get(cv::CAP_PROP_FRAME_WIDTH);
    video.height       = (int)capture.get(cv::CAP_PROP_FRAME_HEIGHT);

    cv::namedWindow("VC - VIDEO", cv::WINDOW_AUTOSIZE);
    cv::Mat frame;

    while (key != 'q') {
        capture.read(frame);
        if (frame.empty()) break;

        video.nframe = (int)capture.get(cv::CAP_PROP_POS_FRAMES);

        // ----------------------------------------------------------
        // 1) Criar imagens IVC
        // ----------------------------------------------------------
        IVC* imageBGR     = vc_image_new(video.width, video.height, 3, 255);
        IVC* imageHSV     = vc_image_new(video.width, video.height, 3, 255);
        IVC* imageMask    = vc_image_new(video.width, video.height, 1, 255);
        IVC* imageDilated = vc_image_new(video.width, video.height, 1, 255);
        IVC* imageEroded  = vc_image_new(video.width, video.height, 1, 255);
        IVC* imageLabels  = vc_image_new(video.width, video.height, 1, 255);

        memcpy(imageBGR->data, frame.data, video.width * video.height * 3);

        // ----------------------------------------------------------
        // 2) Pipeline de segmentação
        // ----------------------------------------------------------
        vc_bgr_to_hsv(imageBGR, imageHSV);
        vc_hsv_segmentation(imageHSV, imageMask, 5, 35, 40, 100, 30, 100);
        vc_binary_dilate(imageMask, imageDilated, 15);
        vc_binary_erode(imageDilated, imageEroded, 15);

        // ----------------------------------------------------------
        // 3) Labeling
        // ----------------------------------------------------------
        int  nlabels = 0;
        OVC* blobs   = vc_binary_blob_labelling(imageEroded, imageLabels, &nlabels);

        if (blobs != NULL && nlabels > 0)
        {
            vc_binary_blob_info(imageLabels, blobs, nlabels);

            for (int i = 0; i < nlabels; i++)
            {
                if (blobs[i].area < 3000) continue;   // ruído

                int   cx     = blobs[i].xc;
                int   cy     = blobs[i].yc;
                int   blobY  = blobs[i].y;            // topo da bounding-box

                float diam_mm = calcDiametroMM(blobs[i].width, blobs[i].height);
                int   calibre = calcCalibre(diam_mm);
                const char* cat = calcCategoria(blobs[i].area, blobs[i].perimeter);

                // -----------------------------------------------
                // Associar a um track existente (vizinho mais próximo)
                // -----------------------------------------------
                int   idxProximo = -1;
                float distMin    = 99999.0f;

                for (int j = 0; j < (int)rastreio.size(); j++) {
                    float dx   = (float)(cx - rastreio[j].cx);
                    float dy   = (float)(cy - rastreio[j].cy);
                    float dist = sqrtf(dx*dx + dy*dy);
                    if (dist < distMin) {
                        distMin    = dist;
                        idxProximo = j;
                    }
                }

                if (idxProximo >= 0 && distMin < DIST_MAX) {
                    // Atualizar track existente
                    BlobTrack& t    = rastreio[idxProximo];
                    t.cx            = cx;
                    t.cy            = cy;
                    t.blobY         = blobY;
                    t.frameVista    = video.nframe;
                    t.diametro_mm   = diam_mm;
                    t.calibre       = calibre;
                    strncpy(t.categoria, cat, sizeof(t.categoria)-1);
                    t.area          = blobs[i].area;
                    t.perimetro     = blobs[i].perimeter;

                    // -------------------------------------------
                    // CONTAGEM: topo da bbox ultrapassou a linha
                    //           e blob ainda não foi contado
                    // -------------------------------------------
                    if (!t.contado && blobY >= LINHA_CONTAGEM) {
                        t.contado = true;

                        LaranjaInfo info;
                        info.id          = proximoId++;
                        info.diametro_mm = diam_mm;
                        info.calibre     = calibre;
                        strncpy(info.categoria, cat, sizeof(info.categoria)-1);
                        info.area        = blobs[i].area;
                        info.perimetro   = blobs[i].perimeter;
                        info.cx          = cx;
                        info.cy          = cy;
                        laranjas.push_back(info);

                        std::cout << "[CONTAGEM] Laranja #" << info.id
                                  << "  Cat:" << info.categoria
                                  << "  D:"   << info.diametro_mm << "mm"
                                  << "  Cal:" << info.calibre
                                  << "  A:"   << info.area
                                  << "  P:"   << info.perimetro
                                  << "\n";
                    }
                }
                else {
                    // Blob novo — criar track
                    BlobTrack nova;
                    nova.cx          = cx;
                    nova.cy          = cy;
                    nova.blobY       = blobY;
                    nova.frameVista  = video.nframe;
                    // Se entrou já abaixo da linha (ex: vídeo começou a meio)
                    // considera contado para não duplicar
                    nova.contado     = (blobY >= LINHA_CONTAGEM);
                    nova.diametro_mm = diam_mm;
                    nova.calibre     = calibre;
                    strncpy(nova.categoria, cat, sizeof(nova.categoria)-1);
                    nova.area        = blobs[i].area;
                    nova.perimetro   = blobs[i].perimeter;
                    rastreio.push_back(nova);

                    // Se entrou já abaixo da linha, conta imediatamente
                    if (nova.contado) {
                        LaranjaInfo info;
                        info.id          = proximoId++;
                        info.diametro_mm = diam_mm;
                        info.calibre     = calibre;
                        strncpy(info.categoria, cat, sizeof(info.categoria)-1);
                        info.area        = blobs[i].area;
                        info.perimetro   = blobs[i].perimeter;
                        info.cx          = cx;
                        info.cy          = cy;
                        laranjas.push_back(info);

                        std::cout << "[CONTAGEM] Laranja #" << info.id
                                  << "  Cat:" << info.categoria
                                  << "  D:"   << info.diametro_mm << "mm"
                                  << "  Cal:" << info.calibre
                                  << "  A:"   << info.area
                                  << "  P:"   << info.perimetro
                                  << "\n";
                    }
                }

                // -----------------------------------------------
                // Cor do rectângulo por categoria
                // -----------------------------------------------
                cv::Scalar corRect;
                if      (strcmp(cat, "Extra") == 0) corRect = cv::Scalar(0, 255, 0);
                else if (strcmp(cat, "I")     == 0) corRect = cv::Scalar(255, 255, 0);
                else if (strcmp(cat, "II")    == 0) corRect = cv::Scalar(0, 165, 255);
                else if (strcmp(cat, "III")   == 0) corRect = cv::Scalar(0, 0, 255);
                else                                corRect = cv::Scalar(128, 128, 128);

                // -----------------------------------------------
                // Desenho — bounding-box + crosshair + labels
                // -----------------------------------------------
                cv::rectangle(frame,
                    cv::Point(blobs[i].x, blobs[i].y),
                    cv::Point(blobs[i].x + blobs[i].width,
                              blobs[i].y + blobs[i].height),
                    corRect, 2);

                cv::line(frame, cv::Point(cx-8, cy), cv::Point(cx+8, cy),
                         cv::Scalar(255,255,255), 2);
                cv::line(frame, cv::Point(cx, cy-8), cv::Point(cx, cy+8),
                         cv::Scalar(255,255,255), 2);

                int textY = blobs[i].y - 10;
                if (textY < 60) textY = blobs[i].y + blobs[i].height + 20;

                char buf[128];

                sprintf(buf, "A:%d P:%d", blobs[i].area, blobs[i].perimeter);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(0,0,0), 2);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(255,255,255), 1);

                if (calibre >= 0)
                    sprintf(buf, "D:%.1fmm CAL:%d", diam_mm, calibre);
                else
                    sprintf(buf, "D:%.1fmm CAL:<min", diam_mm);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY+16),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(0,0,0), 2);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY+16),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45, corRect, 1);

                sprintf(buf, "CAT:%s", cat);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY+32),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(0,0,0), 2);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY+32),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45, corRect, 1);

                sprintf(buf, "CG:(%d,%d)", cx, cy);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY+48),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(0,0,0), 2);
                cv::putText(frame, buf, cv::Point(blobs[i].x, textY+48),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(255,255,255), 1);

                // Mostrar ID se já foi contada
                for (const auto& li : laranjas) {
                    if (abs(li.cx - cx) < (int)DIST_MAX &&
                        abs(li.cy - cy) < (int)DIST_MAX)
                    {
                        sprintf(buf, "#%d", li.id);
                        cv::putText(frame, buf,
                                    cv::Point(blobs[i].x, textY+64),
                                    cv::FONT_HERSHEY_SIMPLEX, 0.55,
                                    cv::Scalar(0,0,0), 3);
                        cv::putText(frame, buf,
                                    cv::Point(blobs[i].x, textY+64),
                                    cv::FONT_HERSHEY_SIMPLEX, 0.55,
                                    cv::Scalar(0,255,255), 1);
                        break;
                    }
                }
            }

            free(blobs);
        }

        // ----------------------------------------------------------
        // 4) Remove tracks ausentes há demasiadas frames
        // ----------------------------------------------------------
        for (int j = (int)rastreio.size() - 1; j >= 0; j--) {
            if (video.nframe - rastreio[j].frameVista > FRAMES_AUSENTE)
                rastreio.erase(rastreio.begin() + j);
        }

        // ----------------------------------------------------------
        // 5) Linha de contagem visual
        // ----------------------------------------------------------
        cv::line(frame,
                 cv::Point(0, LINHA_CONTAGEM),
                 cv::Point(video.width, LINHA_CONTAGEM),
                 cv::Scalar(0, 0, 255), 2);

        cv::putText(frame, "LINHA CONTAGEM",
                    cv::Point(video.width / 2 - 80, LINHA_CONTAGEM - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 0, 255), 1);

        // ----------------------------------------------------------
        // 6) HUD — informação geral
        // ----------------------------------------------------------
        str = "TOTAL CONTADAS: " + std::to_string((int)laranjas.size());
        cv::putText(frame, str, cv::Point(20, LINHA_CONTAGEM + 25),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0,0,0), 2);
        cv::putText(frame, str, cv::Point(20, LINHA_CONTAGEM + 25),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0,200,0), 1);

        str = "FRAME: " + std::to_string(video.nframe)
            + "/" + std::to_string(video.ntotalframes);
        cv::putText(frame, str, cv::Point(20, LINHA_CONTAGEM + 50),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(0,0,0), 2);
        cv::putText(frame, str, cv::Point(20, LINHA_CONTAGEM + 50),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(255,255,255), 1);

        // Legenda de categorias
        cv::putText(frame, "Extra",   cv::Point(10, video.height-80),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,255,0),   1);
        cv::putText(frame, "Cat.I",   cv::Point(10, video.height-60),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,0), 1);
        cv::putText(frame, "Cat.II",  cv::Point(10, video.height-40),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,165,255), 1);
        cv::putText(frame, "Cat.III", cv::Point(10, video.height-20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,255),   1);

        // ----------------------------------------------------------
        // 7) Libertar memória IVC
        // ----------------------------------------------------------
        vc_image_free(imageBGR);
        vc_image_free(imageHSV);
        vc_image_free(imageMask);
        vc_image_free(imageDilated);
        vc_image_free(imageEroded);
        vc_image_free(imageLabels);

        cv::imshow("VC - VIDEO", frame);
        key = cv::waitKey(0);
    }

    // ==============================================================
    // Resumo final no terminal
    // ==============================================================
    std::cout << "\n===== RESUMO FINAL =====\n";
    std::cout << "Total de laranjas contadas: " << laranjas.size() << "\n\n";
    std::cout << std::left
              << std::setw(6)  << "ID"
              << std::setw(10) << "Categ."
              << std::setw(12) << "Diam(mm)"
              << std::setw(8)  << "Calibre"
              << std::setw(10) << "Area"
              << std::setw(12) << "Perimetro"
              << "\n";
    std::cout << std::string(58, '-') << "\n";
    for (const auto& l : laranjas) {
        std::cout << std::left
                  << std::setw(6)  << l.id
                  << std::setw(10) << l.categoria
                  << std::setw(12) << l.diametro_mm
                  << std::setw(8)  << l.calibre
                  << std::setw(10) << l.area
                  << std::setw(12) << l.perimetro
                  << "\n";
    }

    cv::destroyWindow("VC - VIDEO");
    capture.release();
    return 0;
}