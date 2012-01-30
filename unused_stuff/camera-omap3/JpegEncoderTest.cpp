#include <stdlib.h>
#include "JpegEncoder.h"


int main(int argc, char **argv)
{
    char inFilename[50];
    const char *inputfilename = "JPGE_CONF_003.yuv";
    char outFilename[50];
    const char *outputfilename= "/E";
    char path[50];
    int w = 176;
    int h= 144;
    int count = 1;
    int quality = 100;
    int inbufferlen = 0;
    int outbufferlen;
    FILE* f = NULL;
    JpegEncoder *JpegEnc = new JpegEncoder();


    if (argc == 2){
        printf("\n\nUsage: ./system/bin/JpegEncoderTest <count> <input filename> <output filename> <width> <height> <quality>\n\n");
        count = atoi(argv[1]);
        strcpy(inFilename, inputfilename);
        strcpy(outFilename, outputfilename);
        printf("\nCount = %d", count);
        printf("\nUsing Default Parameters for the rest");
        printf("\nInput Filename = %s", inFilename);
        printf("\nOutput Filename = %s", outFilename);
        printf("\nw = %d", w);
        printf("\nh = %d", h);
        printf("\nquality = %d", quality);
    }
    else if (argc != 8){
        printf("\n\nUsage: ./system/bin/SkImageEncoderTest <count> <input filename> <output filename> <width> <height> <quality>\n\n");
        strcpy(inFilename, inputfilename);
        strcpy(outFilename, outputfilename);
        printf("\nUsing Default Parameters");
        printf("\nCount: Single Encode");
        printf("\nInput Filename = %s", inFilename);
        printf("\nOutput Filename = %s", outFilename);
        printf("\nw = %d", w);
        printf("\nh = %d", h);
        printf("\nquality = %d", quality);
    }
    else{
        count = atoi(argv[1]);
        strcpy(inFilename, argv[2]);
        strcpy(outFilename, argv[3]);
        w = atoi(argv[4]);
        h = atoi(argv[5]);
        quality = atoi(argv[6]);
    }

    FILE* fIn = NULL;
    fIn = fopen(inFilename, "r");
    if ( fIn == NULL ) {
        printf("\nError: failed to open the file %s for reading", inFilename);
        return 0;
    }

    printf("\nOpened File %s\n", inFilename);

    inbufferlen = w * h * 2;
    void *inBuffer = malloc(inbufferlen + 256);
    inBuffer = (void *)((int)inBuffer + 128);
    fread((void *)inBuffer,  1, inbufferlen, fIn);
    if ( fIn != NULL ) fclose(fIn);

    outbufferlen =  (w * h) + 12288;
    void *outBuffer = malloc(outbufferlen + 256);
    outBuffer = (void *)((int)outBuffer + 128);


    for (int i = 0; i < count; i++)
    {
        printf("\n\nCalling encodeImage. Round %d. \n\n", i);

        if (JpegEnc->encodeImage(outBuffer, outbufferlen, inBuffer, inbufferlen, w, h, quality, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)){

            printf("\n\n encodeImage Completed. Round %d.\n\n", i);

            snprintf(path, sizeof(path), "%s_%d.jpg", outFilename, i);

            f = fopen(path, "w");
            if ( f == NULL ) {
                printf("\nError: failed to open the file %s for writing", path);
                return 0;
            }

            fwrite(outBuffer,  1, JpegEnc->jpegSize, f);

            if ( f != NULL ) fclose(f);

            printf("Test Successful\n");
        }
        else
        {
            printf("Test UnSuccessful\n");
            break;
        }
    }

    inBuffer = (void*)((int)inBuffer - 128);
    outBuffer = (void*)((int)outBuffer - 128);
    free(inBuffer);
    free(outBuffer);
    delete JpegEnc;
    return 0;
}

 
