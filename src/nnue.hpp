#pragma once

// clang-format off

namespace nnue
{
    const char* NET_FILE = "nn.nnue";
    const u16 HIDDEN_LAYER_SIZE = 384;
    const i32 SCALE = 400, 
              Q = 255 * 64, 
              NORMALIZATION_K = 1;

    struct alignas(64) NN {
        array<i16, 768 * HIDDEN_LAYER_SIZE> featureWeights;
        array<i16, HIDDEN_LAYER_SIZE> featureBiases;
        array<i8, HIDDEN_LAYER_SIZE * 2> outputWeights;
        i16 outputBias;
    };

    NN nn;

    inline void loadNetFromFile()
    {        
        FILE *netFile;

        #if defined(_WIN32) // windows 32 or 64
        int error = fopen_s(&netFile, NET_FILE, "rb");
        if (error != 0)
        {
            cout << "Error opening net file " << NET_FILE << endl;
            exit(0);
        }

        #elif defined(__unix__) // linux
        netFile = fopen(NET_FILE, "rb");
        if (netFile == NULL)
        {
            cout << "Error opening net file " << NET_FILE << endl;
            exit(0);
        }
        #endif

        // Read binary data (weights and biases) into the struct
        fread(nn.featureWeights.data(), sizeof(i16), nn.featureWeights.size(), netFile);
        fread(nn.featureBiases.data(), sizeof(i16), nn.featureBiases.size(), netFile);
        fread(nn.outputWeights.data(), sizeof(i8), nn.outputWeights.size(), netFile);
        fread(&nn.outputBias, sizeof(i16), 1, netFile);
        fclose(netFile); 
    }

    struct Accumulator
    {
        i16 white[HIDDEN_LAYER_SIZE];
        i16 black[HIDDEN_LAYER_SIZE];

        inline Accumulator()
        {
            for (int i = 0; i < HIDDEN_LAYER_SIZE; i++)
                white[i] = black[i] = nn.featureBiases[i];
        }

        inline void activate(Color color, Square sq, PieceType pieceType)
        {
            int whiteIdx = (int)color * 384 + (int)pieceType * 64 + sq;
            int blackIdx = !(int)color * 384 + (int)pieceType * 64 + (sq ^ 56);
            int whiteOffset = whiteIdx * HIDDEN_LAYER_SIZE;
            int blackOffset = blackIdx * HIDDEN_LAYER_SIZE;

            for (int i = 0; i < HIDDEN_LAYER_SIZE; i++)
            {
                white[i] += nn.featureWeights[whiteOffset + i];
                black[i] += nn.featureWeights[blackOffset + i];
            }
        }   

        inline void deactivate(Color color, Square sq, PieceType pieceType)
        {
            int whiteIdx = (int)color * 384 + (int)pieceType * 64 + sq;
            int blackIdx = !(int)color * 384 + (int)pieceType * 64 + (sq ^ 56);
            int whiteOffset = whiteIdx * HIDDEN_LAYER_SIZE;
            int blackOffset = blackIdx * HIDDEN_LAYER_SIZE;

            for (int i = 0; i < HIDDEN_LAYER_SIZE; i++)
            {
                white[i] -= nn.featureWeights[whiteOffset + i];
                black[i] -= nn.featureWeights[blackOffset + i];
            }
        }
    };

    vector<Accumulator> accumulators;
    Accumulator *currentAccumulator;

    inline void reset()
    {
        accumulators.clear();
        accumulators.reserve(256);
        accumulators.push_back(Accumulator());
        currentAccumulator = &accumulators.back();
    }

    inline void push()
    {
        assert(currentAccumulator == &accumulators.back());
        accumulators.push_back(*currentAccumulator);
        currentAccumulator = &accumulators.back();
    }

    inline void pull()
    {
        accumulators.pop_back();
        currentAccumulator = &accumulators.back();
    }

    inline i32 crelu(i32 x)
    {
        return clamp(x, 0, 255);
    }

    inline i32 evaluate(Color color)
    {
        i16 *us = currentAccumulator->white,
            *them = currentAccumulator->black;

        if (color == Color::BLACK)
        {
            us = currentAccumulator->black;
            them = currentAccumulator->white;
        }

        i32 sum = 0;
        for (int i = 0; i < HIDDEN_LAYER_SIZE; i++)
        {
            sum += crelu(us[i]) * nn.outputWeights[i];
            sum += crelu(them[i]) * nn.outputWeights[HIDDEN_LAYER_SIZE + i];
        }

        return (sum / NORMALIZATION_K + nn.outputBias) * SCALE / Q;
    }

}

