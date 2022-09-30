#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <stdint.h>
#endif

#define PREAMBLE_LENGTH 72 //min 576 bits
#define FRAME_LENGTH 64 //1 batch = 8 frames * 8bytes
#define NUM_FRAMES (FRAME_LENGTH / 4) //16x 4bytes = 64bytes

#define PREAMBLE_FILL 0xAA
#define FUNCTION_CODE 0x03 //3 is for alpha mode


#define NUM_BITS_INT (sizeof(uint32_t)*8 -1)

#define MAX_MSG_LENGTH 40
#define DEST_FILE "pocsag.bin"
#define DEFAULT_ADDRESS 1234567

const unsigned int FRAMESYNC_CODEWORD = 0x7CD215D8;
const unsigned int IDLE_CODEWORD = 0x7A89C197;

// Store a chain of POCSAG encoded frames
struct FrameStruct
{
    uint32_t* framePtr;
    int length;
};

// Store a c string converted to an array of 7bit "bytes"
struct Ascii7BitStruct
{
    unsigned char *asciiPtr;
    int length;
};

// Bit reverse
uint8_t bitReverse8(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

// Calculate the BCH 31 21 5 checksum
void calculateBCH3121sum (uint32_t *x)
{
    #define ADDRESS_MASK 0xFFFFF800 //valid data is from POCSAG bits 1-21, 22-31 is the BCH, 32 is parity

    // In order to save time use a pre-computed polynomial, g(x) = 10010110111
    #define G_X 0x769

    const int k = 21;
    uint32_t generator = G_X << k;
    const int n = NUM_BITS_INT; //31

    //dividend is the bits in the data
    *x &= ADDRESS_MASK;

    uint32_t dividend = *x;

    uint32_t mask = 1 << n;

    for ( int i = 0; i < k; i++ )
    {
        if ( dividend & mask )
            dividend ^= generator;
        generator >>= 1;
        mask >>= 1;
    }

    *x |= dividend;
}


void calculateEvenParity (uint32_t *x)
{
    int count = 0;

    for (int i = 1; i < NUM_BITS_INT; i++)
    {
        if ((*x) & (1 << i))
        {
            // There is a 1 bit
            count++;
        }
        //go to next bit
    }

    // If count is even, the parity bit is 0
    count = count % 2;

    *x |= count;
}

// Get 18bits for address field
uint32_t encodeAddress(uint32_t address)
{
    address >>= 3; // remove least significant 3 bits as these form the offset in a batch.

    address &= 0x0007FFFF;
    address <<= 2;

    // add the function bits
    address |= FUNCTION_CODE;

    address <<= 11;

    calculateBCH3121sum(&address);
    calculateEvenParity(&address);

    return htonl(address);
}

// Encode regular C string into 7bit ascii string.
struct Ascii7BitStruct* ascii7bitEncoder(const char* message)
{
    int length = strlen(message);

    int encoded_length = (int)((float)7/8 * length);

    unsigned char* encoded = calloc(sizeof(unsigned char), encoded_length + 1);

    int shift = 1; //count of number of bits to right of each 7bit char
    uint8_t* curr = (uint8_t *) encoded;

    // remove the 8th bit then reverse, shift and pack
    for (int i = 0; i < length; i++)
    {
        uint16_t tmp = bitReverse8(message[i]);

        tmp &= 0x00fe;
        tmp >>= 1;

        tmp <<= shift;

        *curr |= (unsigned char)(tmp & 0x00ff);
        if (curr > encoded)
            *(curr - 1) |= (unsigned char)((tmp & 0xff00) >> 8);

        shift++;

        if (shift == 8)
            shift = 0;
        else
            if (length > 1)
                curr++;
    }

    struct Ascii7BitStruct *encodedString = malloc(sizeof(struct Ascii7BitStruct));
    encodedString->asciiPtr = encoded;
    encodedString->length = encoded_length + 1;

    return encodedString;
}

// Split the 7bit ascii string into frames of 20bit messages + chksum + parity
struct FrameStruct* splitMessageIntoFrames(struct Ascii7BitStruct *ascii7bitBuffer)
{
    // 20bits of message
    int chunks = (ascii7bitBuffer->length / 3) + 1;

    uint32_t * batches = calloc(sizeof(uint32_t), chunks);

    unsigned char* curr = ascii7bitBuffer->asciiPtr;

    const unsigned char* end = curr + ascii7bitBuffer->length;

    for (int i = 0; i < chunks; i++)
    {
        if (end - curr >= 3)
            memcpy((unsigned char*)&batches[i], curr, 3);
        else
            memcpy((unsigned char*)&batches[i], curr, end - curr);

        batches[i] = htonl(batches[i]);

        if (!(i % 2))
        {
            if (end - curr >= 3)
                curr += 2;
            batches[i] &= 0xfffff000;
            batches[i] >>= 1;
        }
        else
        {
            if (end - curr >= 3)
                curr += 3;
            batches[i] &= 0x0fffff00;
            batches[i] <<= 3;
        }

        batches[i] |= (1 << NUM_BITS_INT); // set MSB, to signify that it is a message and not an address
        calculateBCH3121sum(&batches[i]);
        calculateEvenParity(&batches[i]);


        batches[i] = htonl(batches[i]);
    }

    curr = NULL;

    struct FrameStruct *frames = malloc(sizeof(struct FrameStruct));

    frames->framePtr = batches;
    frames->length = chunks;

    return frames;
}

void usage(const char *argv0)
{
    fprintf(stderr, "USAGE: %s [-a address] [-m message] [-f destination]\n", argv0);
    fprintf(stderr, "%s", "a destination file of - will write to stdout instead of file.\n\n");
}


int main(int argc, const char* argv[])
{
    char *dest_file = calloc(1, 128);
    strcpy(dest_file, DEST_FILE);


    char message[MAX_MSG_LENGTH];
    memset(message, 0, MAX_MSG_LENGTH);

    uint32_t address = DEFAULT_ADDRESS;

    // parse the args
    if (argc == 1)
    {
        free(dest_file);
        usage(argv[0]);
        return 1;
    }
    
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-a"))
        {
            // There is -a argument, the next argument will set the address.

            char* badchars = NULL;

            address = strtoul(argv[++i], &badchars, 0);

            if (strlen(badchars) > 0)
            {
                free(dest_file);
                fprintf(stderr, "%s", "Numeric address only.\n\n");
                return 1;
            }


            if (!address)
            {
                address = DEFAULT_ADDRESS;
            }
        }

        else if (!strcmp(argv[i], "-m"))
        {
            // There is a -m argument, the next argument will set the message
            strncpy(message, argv[++i], MAX_MSG_LENGTH - 2);

            const int end = strlen(message);

            message[end] = '\x03'; //end message with a ETX (\x03)
        }

        else if (!strcmp(argv[i], "-f"))
        {
            // There is a -f argument, the next argument will set the output file
            i++;
            free(dest_file);
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
        }
        else
        {
            usage(argv[0]);
            free(dest_file);
            return 1;
        }
    }


    const uint32_t framesync_codeword = htonl(FRAMESYNC_CODEWORD);
    const uint32_t idle_codeword = htonl(IDLE_CODEWORD);

    unsigned char preamble[PREAMBLE_LENGTH];
    // Copy the preamble to the destination buffer
    memset(preamble, PREAMBLE_FILL, PREAMBLE_LENGTH);


    // frame number in batch where the transmission starts
    const int frame_offset = address & 0x7;

    const uint32_t encodedAddress = encodeAddress(address);

    // encode the message
    struct FrameStruct *messageFrames;

    if (!strlen(message))
    {
        // if zero length message, encode an idle frame.
        messageFrames = malloc(sizeof(struct FrameStruct));
        messageFrames->length = 1;
        uint32_t *data = malloc(sizeof(uint32_t));
        messageFrames->framePtr = data;
        memcpy(data, &idle_codeword, sizeof(uint32_t));
    }
    else
    {
        struct Ascii7BitStruct *encodedMessage = ascii7bitEncoder(message);

        messageFrames = splitMessageIntoFrames(encodedMessage);
        free(encodedMessage->asciiPtr);
        free(encodedMessage);
        encodedMessage = NULL;
    }


    int index = 0; //keep track of current bytes written

    unsigned char packet[4 + FRAME_LENGTH];
    memset(packet, 0, sizeof(packet));
    memcpy(packet, &framesync_codeword, 4);
    index += 4;


    // write the preamble to the file
    FILE *fp; // the output file
    if (!dest_file)
        fp = stdout;
    else
    {
        fp = fopen(dest_file, "w");
        if (!fp)
        {
            // handle file open errors
            perror("");
            exit(3);
        }
    }

    fwrite(preamble, 1, PREAMBLE_LENGTH, fp);


    //int frames_left = ((frame_offset + messageFrames->length + NUM_FRAMES - 1) / NUM_FRAMES) * NUM_FRAMES;
    int frames_left = (frame_offset + messageFrames->length + NUM_FRAMES - 1);

    int messagePartsDone = 0;
    int codewordsDone = 0;


    //copy the frame sync to the buffer (at the start of each batch)
    // fill in the batches with the data
    while(frames_left > 0)
    {
        index = 4; //skip the first 4 bytes which is always the frame sync code word.
        //Don't need to re-copy the same bytes for each batch.

        if (!codewordsDone && !messagePartsDone)
        {
            for (int i = 0; i < frame_offset; i++)
            {
                // Fill with idles until the required offset for the receiver is found
                memcpy(packet + index, &idle_codeword, 4);
                memcpy(packet + index + 4, &idle_codeword, 4);
                index += 8;
                codewordsDone += 2;
            }


            // skipped to the frame number after the offset.
            // Now add an address code word.
            memcpy(packet + index, &encodedAddress, 4);
            index += 4;
            frames_left -= frame_offset;
            codewordsDone ++;

        }

        // Add the message frames

        for (int i = messagePartsDone; i < messageFrames->length; i++)
        {
            memcpy(packet + index, &(messageFrames->framePtr[i]), 4);
            index += 4;
            messagePartsDone++;
            codewordsDone++;
            frames_left--;
            if (codewordsDone == NUM_FRAMES)
            {
                codewordsDone = 0;
                break;
            }
        }

        // fill the last batch with idles.
        if (messagePartsDone == messageFrames->length)
        {
            frames_left = 0;
            //pad out the rest of the batch with idles
            for (int i = 0; i < (NUM_FRAMES - codewordsDone); i++)
            {
                if (index >= sizeof(packet))
                {
                    break;
                }
                // Fill with idles until the batch is done
                memcpy(packet + index, &idle_codeword, 4);
                index += 4;
            }
        }

        fwrite(packet, 1, 4 + FRAME_LENGTH, fp);
    }

    fclose(fp);

    // Cleanup
    if (dest_file)
    {
        free(dest_file);
        dest_file = NULL;
    }
    
    free(messageFrames->framePtr);
    messageFrames->framePtr = NULL;

    free(messageFrames);
    messageFrames = NULL;

    // Done. POCSAG packet created.
    /* Decode using:
    nc -u -l -p 7355 | sox -t raw -esigned-integer -b16 -r 48000 - -t raw -esigned-integer -b16 -r 22050 - | multimon-ng -a POCSAG1200 -f alpha -v1 -
    * or to test: ./pocsag [-m message] [-a address] -f - | ./bin2audio -i - -o - | sox -t raw -esigned-integer -b16 -r 48000 - -t raw -esigned-integer -b16 -r 22050 - | multimon-ng -a POCSAG1200 -f alpha -v1 -
    */

    return 0;
}

