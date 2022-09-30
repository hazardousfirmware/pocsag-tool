#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// The data
#define BAUD 1200

// The encoded data
#define SYMBOL_HIGH 0xD001
#define SYMBOL_LOW 0x2fff


// Audio format
// PCM 16bit signed
#define SAMPLE_RATE 48000
#define FORMAT_BITS 16
#define FORMAT_CHANNELS 1 //1 = mono channel
#define FORMAT_DATA 0x0001  //1 = PCM
#define CHUNK_SIZE 16

#define HEADER_LENGTH 44


int main(int argc, const char* argv[])
{
    int code = 0;

    // data
    uint32_t baudrate = BAUD;

    // audio information
    const uint32_t bit_width = FORMAT_BITS;
    uint32_t sample_rate = SAMPLE_RATE;
    const uint16_t data_format = FORMAT_DATA;
    const uint16_t channels = FORMAT_CHANNELS;
    const uint32_t cksize = CHUNK_SIZE;

    // file names
    char* dest_file = NULL;
    char* src_file = NULL;

    char dest_file_set = 0;
    char src_file_set = 0;

    // File pointers
    FILE *out = NULL;
    FILE* in = NULL;

    // parse arguments to specify baud, input, output file and sample rate.
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-b"))
        {
            // There is -b argument, the next argument will set the baud rate
            char* badchars = NULL;

            baudrate = strtoul(argv[++i], &badchars, 0);

            if (strlen(badchars) > 0)
            {
                fprintf(stderr, "%s", "Numeric baud rate only.\n\n");
                return 1;
            }

            if (!baudrate)
            {
                baudrate = BAUD;
            }
        }

        else if (!strcmp(argv[i], "-s"))
        {
            // There is -b argument, the next argument will set the sample rate
            char* badchars = NULL;

            sample_rate = strtoul(argv[++i], &badchars, 0);

            if (strlen(badchars) > 0)
            {
                fprintf(stderr, "%s", "Numeric sample rate only.\n\n");
                return 1;
            }

            if (!sample_rate)
            {
                sample_rate = SAMPLE_RATE;
            }
        }

        else if (!strcmp(argv[i], "-o"))
        {
            // There is a -o argument, the next argument will set the output file
            i++;

            // Use STDOUT
            dest_file = NULL;
            if (strcmp(argv[i], "-"))
            {
                if (strlen(argv[i]) > FILENAME_MAX)
                {
                    // prevent long filenames
                    fprintf(stderr, "%s", "Filename too long.\n\n");
                    return 1;
                }
                dest_file = calloc(1, strlen(argv[i]) + 1);
                strcpy(dest_file, argv[i]);
            }
            dest_file_set = 1;
        }

        else if (!strcmp(argv[i], "-i"))
        {
            // There is a -i argument, the next argument will set the input file
            i++;

            // Use STDIN
            src_file = NULL;
            if (strcmp(argv[i], "-"))
            {
                if (strlen(argv[i]) > FILENAME_MAX)
                {
                    // prevent long filenames
                    fprintf(stderr, "%s", "Filename too long.\n\n");
                    return 1;
                }
                src_file = calloc(1, strlen(argv[i]) + 1);
                strcpy(src_file, argv[i]);
            }
            src_file_set = 1;
        }

        else
        {
            fprintf(stderr, "USAGE: %s [-b baud] [-s sample_rate] -i input -o output\n\n", argv[0]);
            fprintf(stderr, "%s", "-o\tdestination file, - will write to stdout instead of file.\n");
            fprintf(stderr, "%s", "-i\tsource file, - will read from stdin instead of file.\n");

            fprintf(stderr, "%s", "-b\tbaudrate of data (default=1200).\n");
            fprintf(stderr, "%s", "-s\taudio sample rate (default=48000).\n\n");
            return 1;
        }
    }

    if (!src_file_set || !dest_file_set)
    {
        fprintf(stderr, "%s", "Filename must be specified.\n");
        code = 1;
        goto error;
    }


    uint32_t numSamples = 0;

    const uint32_t samples_per_symbol = sample_rate / baudrate;
    uint32_t input_size = 0;

    if (src_file)
    {
        in = fopen(src_file, "r");

        if (!in)
        {
            // handle file open errors
            perror("");
            code = 3;
            goto error;
        }
    }
    else
    {
        in = stdin;
    }

    // Build the header
    uint8_t header[HEADER_LENGTH + 1];
    memset(header, 0, sizeof(header)/sizeof(uint8_t));

    uint32_t tmp;
    uint8_t* ptr = header;

    strncpy(ptr, "RIFF", 4); //ckID - 4
    ptr += 4;

    // place holder for audio data size
    ptr += 4;

    //cksize - len=4 (4 + 24 + (8 + M*Nc*Ns + (0 or 1))

    strncpy(ptr, "WAVEfmt ", 8);
    ptr += 8;

    memcpy(ptr, &cksize, 4); //cksize - len=4
    ptr += 4;

    memcpy(ptr, &data_format, 2); //format tag - len=2
    ptr += 2;

    memcpy(ptr, &channels, 2); //Nc
    ptr += 2;

    memcpy(ptr, &sample_rate, 4); //F
    ptr += 4;

    //M = bits/8 * channels, bytes per sec
    tmp = (sample_rate * bit_width * channels)/ 8; //F*M*Nc
    memcpy(ptr, &tmp, 4);
    ptr += 4;

    tmp = (bit_width * channels);//block align
    memcpy(ptr, (uint16_t*) &tmp, 2); //8*M
    ptr += 2;

    memcpy(ptr, (uint16_t*) &bit_width, 2); //bits per sample
    ptr += 2;

    strncpy(ptr, "data", 4); //ckID
    ptr += 4;

    //cksize (data size) Nc*Ns*M
    tmp = numSamples*(bit_width / 8) * channels;
    memcpy(ptr, &tmp, 4);
    ptr += 4;

    // write the header to file
    if (!dest_file)
        out = stdout;
    else
    {
        out = fopen(dest_file, "w");
        if (!out)
        {
            // handle file open errors
            perror("");
            code = 3;
            goto error;
        }
    }

    fwrite(header, 1, HEADER_LENGTH, out);

    // Header done. Now read write the data
    int c;
    uint8_t byte, bit;
    uint16_t sample = 0;
    while ((c = fgetc(in)) != EOF)
    {
        byte = (uint8_t)c;

        for (int i = 7; i >= 0; i--)
        {
            bit = byte & (1 << i);

            //printf("byte: 0x%2x - bit: %i (%i)\n", byte, bit && 1, i);

            if (bit)
                sample = SYMBOL_HIGH;
            else
                sample = SYMBOL_LOW;

            for (int sample_count = 0; sample_count < samples_per_symbol; sample_count++)
            {
                fwrite(&sample, 1, sizeof(uint16_t), out);
            }
        }

        input_size++;
    }

    //number of bits in file * samp_per_symbol, 1 bit = 1 symbol
    numSamples = input_size * 8 * samples_per_symbol;

    tmp = 4 + 24 + 8 + numSamples*(bit_width / 8) * channels;

    rewind(out);
    fseek(out, 4, 0);
    fwrite(&tmp, 1, sizeof(tmp), out);
    //fseek(out, 0, SEEK_END);

error:
    if (out)
        fclose(out);

    if (in)
        fclose(in);

    if (src_file)
        free(src_file);

    if (dest_file)
        free(dest_file);

    return code;
}

