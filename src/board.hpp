// clang-format off

#pragma once

#include "utils.hpp"
#include "move.hpp"
#include "search_params.hpp" // SEE piece values

constexpr u8 CASTLE_SHORT = 0, CASTLE_LONG = 1;

u64 ZOBRIST_COLOR = 0;
MultiArray<u64, 2, 6, 64> ZOBRIST_PIECES = {}; // [color][pieceType][square]
std::array<u64, 8> ZOBRIST_FILES = {}; // [file]

#include "cuckoo.hpp"

inline void initZobrist()
{
    ZOBRIST_COLOR = randomU64();

    for (int pt = 0; pt < 6; pt++)
        for (int sq = 0; sq < 64; sq++)
        {
            ZOBRIST_PIECES[WHITE][pt][sq] = randomU64();
            ZOBRIST_PIECES[BLACK][pt][sq] = randomU64();
        }

    for (int file = 0; file < 8; file++)
        ZOBRIST_FILES[file] = randomU64();
}

struct BoardState {
    public:
    Color colorToMove = Color::WHITE;
    std::array<u64, 2> colorBitboards  = {}; // [color]
    std::array<u64, 6> piecesBitboards = {}; // [pieceType]
    u64 castlingRights = 0;
    Square enPassantSquare = SQUARE_NONE;
    u8 pliesSincePawnOrCapture = 0;
    u16 currentMoveCounter = 1;
    u64 checkers = 0;
    u64 zobristHash = 0;
    u64 pawnsHash = 0;
    std::array<u64, 2> nonPawnsHash = {}; // [pieceColor]
    u16 lastMove = MOVE_NONE.encoded();
    PieceType captured = PieceType::NONE;
} __attribute__((packed));

class Board {
    private:
    std::vector<BoardState> mStates = {};
    BoardState* mState = nullptr;

    public:

    // Copy assignment operator
    Board& operator=(const Board &other) 
    {
        if (this != &other) {
            mStates = other.mStates;
            mState = mStates.empty() ? nullptr : &mStates.back();
        }

        return *this;
    }

    inline Board() = default;

    Board(const Board &other) {
        mStates = other.mStates;
        mState = mStates.empty() ? nullptr : &mStates.back();
    }

    inline Board(std::string fen)
    {
        mStates = {};
        mStates.reserve(512);
        mStates.push_back(BoardState());
        mState = &mStates[0];

        trim(fen);
        std::vector<std::string> fenSplit = splitString(fen, ' ');

        // Parse color to move
        mState->colorToMove = fenSplit[1] == "b" ? Color::BLACK : Color::WHITE;

        if (mState->colorToMove == Color::BLACK)
            mState->zobristHash ^= ZOBRIST_COLOR;

        // Parse pieces

        mState->colorBitboards  = {};
        mState->piecesBitboards = {};

        const std::string fenRows = fenSplit[0];
        int currentRank = 7, currentFile = 0; // iterate ranks from top to bottom, files from left to right

        for (u64 i = 0; i < fenRows.length(); i++)
        {
            char thisChar = fenRows[i];
            if (thisChar == '/')
            {
                currentRank--;
                currentFile = 0;
            }
            else if (isdigit(thisChar))
                currentFile += charToInt(thisChar);
            else
            {
                const Square sq = currentRank * 8 + currentFile;
                const Color color = isupper(thisChar) ? Color::WHITE : Color::BLACK;
                thisChar = std::tolower(thisChar);

                const PieceType pt = thisChar == 'p' ? PieceType::PAWN 
                                   : thisChar == 'n' ? PieceType::KNIGHT 
                                   : thisChar == 'b' ? PieceType::BISHOP 
                                   : thisChar == 'r' ? PieceType::ROOK
                                   : thisChar == 'q' ? PieceType::QUEEN
                                   : PieceType::KING;
                             
                placePiece(color, pt, sq);
                currentFile++;
            }
        }

        // Parse castling rights

        mState->castlingRights = 0;
        const std::string fenCastlingRights = fenSplit[2];

        if (fenCastlingRights != "-") 
        {
            for (u64 i = 0; i < fenCastlingRights.length(); i++)
            {
                const char thisChar = fenCastlingRights[i];
                const Color color = isupper(thisChar) ? Color::WHITE : Color::BLACK;

                const int castlingRight = thisChar == 'K' || thisChar == 'k' 
                                          ? CASTLE_SHORT : CASTLE_LONG;

                mState->castlingRights |= CASTLING_MASKS[(int)color][castlingRight];
            }

            mState->zobristHash ^= mState->castlingRights;
        }

        // Parse en passant target square

        mState->enPassantSquare = SQUARE_NONE;
        const std::string strEnPassantSquare = fenSplit[3];

        if (strEnPassantSquare != "-")
        {
            mState->enPassantSquare = strToSquare(strEnPassantSquare);
            const int file = (int)squareFile(mState->enPassantSquare);
            mState->zobristHash ^= ZOBRIST_FILES[file];
        }

        // Parse last 2 fen tokens
        mState->pliesSincePawnOrCapture = fenSplit.size() >= 5 ? stoi(fenSplit[4]) : 0;
        mState->currentMoveCounter = fenSplit.size() >= 6 ? stoi(fenSplit[5]) : 1;

        mState->checkers = attackers(kingSquare()) & them();
    }

    inline Color sideToMove() const { return mState->colorToMove; }

    inline Color oppSide() const { return oppColor(mState->colorToMove); }

    inline u64 getBb(const PieceType pieceType) const {   
        return mState->piecesBitboards[(int)pieceType];
    }

    inline u64 getBb(const Color color) const { 
        return mState->colorBitboards[(int)color]; 
    }

    inline u64 getBb(const Color color, const PieceType pieceType) const {
        return mState->piecesBitboards[(int)pieceType]
               & mState->colorBitboards[(int)color];
    }

    inline void getColorBitboards(std::array<u64, 2> &colorBitboards) const {
        colorBitboards = mState->colorBitboards;
    }

    inline void getPiecesBitboards(std::array<u64, 6> &piecesBitboards) const {
        piecesBitboards = mState->piecesBitboards;
    }

    inline u64 us() const { 
        return getBb(sideToMove());
    }

    inline u64 them() const { 
        return getBb(oppSide());
    }

    inline u64 occupancy() const { 
        return getBb(Color::WHITE) | getBb(Color::BLACK);
    }

    inline bool isOccupied(const Square square) const {
        return occupancy() & bitboard(square);
    }

    inline u64 checkers() const { return mState->checkers; }

    inline bool inCheck() const { return mState->checkers > 0; }

    inline bool inCheck2PliesAgo() {
        assert(mStates.size() > 2);
        assert(mState == &mStates.back());
        return (mState - 2)->checkers > 0;
    }

    inline u64 zobristHash() const { return mState->zobristHash; }

    inline u64 pawnsHash() const { return mState->pawnsHash; }

    inline u64 nonPawnsHash(Color color) const { return mState->nonPawnsHash[(int)color]; }

    inline Move lastMove() const { return mState->lastMove; }

    inline Move nthToLastMove(const int n) const {
        assert(n >= 1);

        return (int)mStates.size() - n < 0 
               ? MOVE_NONE 
               : Move(mStates[mStates.size() - n].lastMove);
    }

    inline PieceType captured() const { return mState->captured; }
    
    inline PieceType pieceTypeAt(const Square square) const 
    { 
        if (!isOccupied(square)) return PieceType::NONE;

        for (int pt = PAWN; pt <= KING; pt++)
            if (bitboard(square) & getBb((PieceType)pt))
                return (PieceType)pt;

        return PieceType::NONE;
    }

    inline Square kingSquare(const Color color) const {
        return lsb(getBb(color, PieceType::KING));
    }
    
    inline Square kingSquare() const {
        return kingSquare(sideToMove());
    }

    private:

    inline void placePiece(const Color color, const PieceType pieceType, const Square square) 
    {
        assert(!isOccupied(square));

        mState->colorBitboards[(int)color]      |= bitboard(square);
        mState->piecesBitboards[(int)pieceType] |= bitboard(square);

        updateHashes(color, pieceType, square);
    }

    inline void removePiece(const Color color, const PieceType pieceType, const Square square) 
    {
        assert(getBb(color, pieceType) & bitboard(square));

        mState->colorBitboards[(int)color]      ^= bitboard(square);
        mState->piecesBitboards[(int)pieceType] ^= bitboard(square);

        updateHashes(color, pieceType, square);
    }

    inline void updateHashes(const Color color, const PieceType pieceType, const Square square)
    {
        mState->zobristHash ^= ZOBRIST_PIECES[(int)color][(int)pieceType][square];

        if (pieceType == PieceType::PAWN)
            mState->pawnsHash ^= ZOBRIST_PIECES[(int)color][(int)pieceType][square];
        else
            mState->nonPawnsHash[(int)color] ^= ZOBRIST_PIECES[(int)color][(int)pieceType][square];
    }

    public:

    inline std::string fen() const {
        std::string myFen = "";

        for (int rank = 7; rank >= 0; rank--)
        {
            int emptySoFar = 0;

            for (int file = 0; file < 8; file++)
            {
                const Square square = rank * 8 + file;

                if (!isOccupied(square)) {
                    emptySoFar++;
                    continue;
                }

                if (emptySoFar > 0) 
                    myFen += std::to_string(emptySoFar);

                const PieceType pt = pieceTypeAt(square);
                
                char piece = pt == PieceType::PAWN   ? 'p'
                           : pt == PieceType::KNIGHT ? 'n'
                           : pt == PieceType::BISHOP ? 'b'
                           : pt == PieceType::ROOK   ? 'r'
                           : pt == PieceType::QUEEN  ? 'q'
                           : pt == PieceType::KING   ? 'k'
                           : '.';

                if (getBb(Color::WHITE) & bitboard(square))
                    piece = std::toupper(piece);

                myFen += std::string(1, piece);
                emptySoFar = 0;
            }

            if (emptySoFar > 0) 
                myFen += std::to_string(emptySoFar);

            myFen += "/";
        }

        myFen.pop_back(); // remove last '/'

        myFen += sideToMove() == Color::BLACK ? " b " : " w ";

        std::string strCastlingRights = "";
        if (mState->castlingRights & CASTLING_MASKS[WHITE][CASTLE_SHORT]) 
            strCastlingRights += "K";
        if (mState->castlingRights & CASTLING_MASKS[WHITE][CASTLE_LONG]) 
            strCastlingRights += "Q";
        if (mState->castlingRights & CASTLING_MASKS[BLACK][CASTLE_SHORT]) 
            strCastlingRights += "k";
        if (mState->castlingRights & CASTLING_MASKS[BLACK][CASTLE_LONG]) 
            strCastlingRights += "q";

        if (strCastlingRights.size() == 0) 
            strCastlingRights = "-";

        myFen += strCastlingRights;

        myFen += " ";
        myFen += mState->enPassantSquare == SQUARE_NONE 
                 ? "-" : SQUARE_TO_STR[mState->enPassantSquare];
        
        myFen += " " + std::to_string(mState->pliesSincePawnOrCapture);
        myFen += " " + std::to_string(mState->currentMoveCounter);

        return myFen;
    }

    inline void print() const { 
        std::string str = "";

        for (int i = 7; i >= 0; i--) {
            for (int j = 0; j < 8; j++) 
            {
                const int square = i * 8 + j;
                const PieceType pt = pieceTypeAt(square);
                
                char piece = pt == PieceType::PAWN   ? 'p'
                           : pt == PieceType::KNIGHT ? 'n'
                           : pt == PieceType::BISHOP ? 'b'
                           : pt == PieceType::ROOK   ? 'r'
                           : pt == PieceType::QUEEN  ? 'q'
                           : pt == PieceType::KING   ? 'k'
                           : '.';
                           
                if (pt != PieceType::NONE && (getBb(Color::WHITE) & bitboard(square)))
                    piece = std::toupper(piece);

                str += std::string(1, piece) + " ";
            }
            str += "\n";
        }

        std::cout << str << std::endl;
        std::cout << fen() << std::endl;
        std::cout << "Zobrist hash: " << mState->zobristHash << std::endl;

        std::cout << "Moves:";

        for (size_t i = 1; i < mStates.size(); i++)
            std::cout << " " << Move(mStates[i].lastMove).toUci();

        std::cout << std::endl;
    }

    inline bool isCapture(const Move move) const {
        assert(move != MOVE_NONE);
        return isOccupied(move.to()) || move.flag() == Move::EN_PASSANT_FLAG;
    }

    inline PieceType captured(const Move move) const {
        assert(move != MOVE_NONE);

        return move.flag() == Move::EN_PASSANT_FLAG
               ? PieceType::PAWN 
               : pieceTypeAt(move.to());
    }

    inline bool isRepetition(const int searchPly = 100000) const {
        assert(searchPly >= 0);
        
        if (mStates.size() <= 4 || mState->pliesSincePawnOrCapture < 4) 
            return false;

        const int stateIdxAfterPawnOrCapture = 
            std::max(0, (int)mStates.size() - (int)mState->pliesSincePawnOrCapture - 1);

        const int rootStateIdx = (int)mStates.size() - searchPly - 1;
        
        int count = 0;

        for (int i = (int)mStates.size() - 3; i >= stateIdxAfterPawnOrCapture; i -= 2)
            if (mStates[i].zobristHash == mState->zobristHash
            && (i > rootStateIdx || ++count == 2))
                return true;

        return false;
    }

    inline bool isDraw(const int searchPly) const {
        if (mState->pliesSincePawnOrCapture >= 100)
            return true;

        // K vs K
        const int numPieces = std::popcount(occupancy());
        if (numPieces == 2) return true;

        // KB vs K
        // KN vs K
        if (numPieces == 3 
        && (getBb(PieceType::KNIGHT) > 0 || getBb(PieceType::BISHOP) > 0))
            return true;

        return isRepetition(searchPly);
    }

    inline bool isSquareAttacked(const Square square, const Color colorAttacking, const u64 occ) const {
        // Idea: put a super piece in this square and see if its attacks intersect with an enemy piece

        // Pawn
        if (attacks::pawnAttacks(square, oppColor(colorAttacking)) 
        & getBb(colorAttacking, PieceType::PAWN))
            return true;

        // Knight
        if (attacks::knightAttacks(square) 
        & getBb(colorAttacking, PieceType::KNIGHT))
            return true;

        // Bishop and queen
        if (attacks::bishopAttacks(square, occ)
        & (getBb(colorAttacking, PieceType::BISHOP) 
        | getBb(colorAttacking, PieceType::QUEEN)))
            return true;
 
        // Rook and queen
        if (attacks::rookAttacks(square, occ)
        & (getBb(colorAttacking, PieceType::ROOK) 
        | getBb(colorAttacking, PieceType::QUEEN)))
            return true;

        // King
        if (attacks::kingAttacks(square) 
        & getBb(colorAttacking, PieceType::KING)) 
            return true;

        return false;
    }

    inline bool isSquareAttacked(const Square square, const Color colorAttacking) const {
        return isSquareAttacked(square, colorAttacking, occupancy());
    }

    inline u64 attackers(const Square square, const u64 occ) const
    {
        const u64 bishopsQueens = getBb(PieceType::BISHOP) | getBb(PieceType::QUEEN);
        const u64 rooksQueens   = getBb(PieceType::ROOK)   | getBb(PieceType::QUEEN);

        u64 attackers = getBb(Color::BLACK, PieceType::PAWN) 
                        & attacks::pawnAttacks(square, Color::WHITE);

        attackers |= getBb(Color::WHITE, PieceType::PAWN) 
                     & attacks::pawnAttacks(square, Color::BLACK);

        attackers |= getBb(PieceType::KNIGHT) & attacks::knightAttacks(square);

        attackers |= bishopsQueens & attacks::bishopAttacks(square, occ);
        attackers |= rooksQueens   & attacks::rookAttacks(square, occ);

        attackers |= getBb(PieceType::KING) & attacks::kingAttacks(square);

        return attackers;
    }

    inline u64 attackers(const Square square) const {
        return attackers(square, occupancy());
    }

    inline u64 attacks(const Color color, const u64 occ) const
    {
        u64 attacksBb = 0;

        u64 pawns = getBb(color) & getBb(PieceType::PAWN);
        while (pawns) {
            const Square sq = poplsb(pawns);
            attacksBb |= attacks::pawnAttacks(sq, color);
        }

        u64 knights = getBb(color) & getBb(PieceType::KNIGHT);
        while (knights) {
            const Square sq = poplsb(knights);
            attacksBb |= attacks::knightAttacks(sq);
        }

        u64 bishopsQueens = getBb(color) & (getBb(PieceType::BISHOP) | getBb(PieceType::QUEEN));

        while (bishopsQueens) {
            const Square sq = poplsb(bishopsQueens);
            attacksBb |= attacks::bishopAttacks(sq, occ);
        }

        u64 rooksQueens = getBb(color) & (getBb(PieceType::ROOK) | getBb(PieceType::QUEEN));

        while (rooksQueens) {
            const Square sq = poplsb(rooksQueens);
            attacksBb |= attacks::rookAttacks(sq, occ);
        }

        attacksBb |= attacks::kingAttacks(kingSquare(color));

        assert(attacksBb > 0);
        return attacksBb;
    } 

    inline u64 attacks(const Color color) const { 
        return attacks(color, occupancy()); 
    }

    inline u64 pinned() const { 
        const Square kingSquare = this->kingSquare();
        const u64 theirBishopsQueens = them() & (getBb(PieceType::BISHOP) | getBb(PieceType::QUEEN));
        const u64 theirRooksQueens   = them() & (getBb(PieceType::ROOK)   | getBb(PieceType::QUEEN));

        u64 potentialAttackers = theirBishopsQueens & attacks::bishopAttacks(kingSquare, them());
        potentialAttackers    |= theirRooksQueens   & attacks::rookAttacks(kingSquare, them());  
    
        u64 pinnedBitboard = 0;

        while (potentialAttackers > 0) {
            const Square attackerSquare = poplsb(potentialAttackers);
            const u64 maybePinned = us() & BETWEEN[attackerSquare][kingSquare];

            if (std::popcount(maybePinned) == 1)
                pinnedBitboard |= maybePinned;
        }

        return pinnedBitboard;
     }

     // SEE (Static exchange evaluation)
    inline bool SEE(const Move move, const i32 threshold = 0) const
    {
        assert(move != MOVE_NONE);

        const std::array<i32, 7> SEE_PIECE_VALUES = {
            seePawnValue(),  // Pawn
            seeMinorValue(), // Knight
            seeMinorValue(), // Bishop
            seeRookValue(),  // Rook
            seeQueenValue(), // Queen
            0,               // King
            0                // None
        };

        i32 score = -threshold;

        const PieceType captured = this->captured(move);
        score += SEE_PIECE_VALUES[(int)captured];

        const PieceType promotion = move.promotion();

        if (promotion != PieceType::NONE)
            score += SEE_PIECE_VALUES[(int)promotion] - SEE_PIECE_VALUES[PAWN];

        if (score < 0) return false;

        PieceType next = promotion != PieceType::NONE ? promotion : move.pieceType();
        score -= SEE_PIECE_VALUES[(int)next];

        if (score >= 0) return true;

        const Square from = move.from();
        const Square square = move.to();

        const u64 bishopsQueens = getBb(PieceType::BISHOP) | getBb(PieceType::QUEEN);
        const u64 rooksQueens   = getBb(PieceType::ROOK)   | getBb(PieceType::QUEEN);

        u64 occupancy = this->occupancy() ^ bitboard(from) ^ bitboard(square);
        u64 attackers = this->attackers(square, occupancy);

        Color us = oppSide();

        auto popLeastValuable = [&] (const u64 attackers) -> PieceType 
        {
            for (int pt = PAWN; pt <= KING; pt++)
            {
                const u64 bb = attackers & getBb(us, (PieceType)pt);

                if (bb > 0) {
                    const Square sq = lsb(bb);
                    occupancy ^= bitboard(sq);
                    return (PieceType)pt;
                }
            }

            return PieceType::NONE;
        };

        while (true)
        {
            const u64 ourAttackers = attackers & getBb(us);

            if (ourAttackers == 0) break;

            next = popLeastValuable(ourAttackers);

            if (next == PieceType::PAWN || next == PieceType::BISHOP || next == PieceType::QUEEN)
                attackers |= attacks::bishopAttacks(square, occupancy) & bishopsQueens;

            if (next == PieceType::ROOK || next == PieceType::QUEEN)
                attackers |= attacks::rookAttacks(square, occupancy) & rooksQueens;

            attackers &= occupancy;
            score = -score - 1 - SEE_PIECE_VALUES[(int)next];
            us = oppColor(us);

            // if our only attacker is our king, but the opponent still has defenders
            if (score >= 0 
            && next == PieceType::KING 
            && (attackers & getBb(us)) > 0)
                us = oppColor(us);

            if (score >= 0) break;
        }

        return sideToMove() != us;
    }

    inline Move uciToMove(const std::string uciMove) const
    {
        const Square from = strToSquare(uciMove.substr(0,2));
        const Square to = strToSquare(uciMove.substr(2,4));
        const PieceType pieceType = pieceTypeAt(from);
        u16 moveFlag = (u16)pieceType + 1;

        if (uciMove.size() == 5) // promotion
        {
            const char promotionLowerCase = uciMove.back(); // last char of string

            if (promotionLowerCase == 'n') 
                moveFlag = Move::KNIGHT_PROMOTION_FLAG;
            else if (promotionLowerCase == 'b') 
                moveFlag = Move::BISHOP_PROMOTION_FLAG;
            else if (promotionLowerCase == 'r') 
                moveFlag = Move::ROOK_PROMOTION_FLAG;
            else
                moveFlag = Move::QUEEN_PROMOTION_FLAG;
        }
        else if (pieceType == PieceType::KING)
        {
            if (abs((int)to - (int)from) == 2)
                moveFlag = Move::CASTLING_FLAG;
        }
        else if (pieceType == PieceType::PAWN)
        { 
            const int bitboardSquaresTraveled = abs((int)to - (int)from);

            if (bitboardSquaresTraveled == 16)
                moveFlag = Move::PAWN_TWO_UP_FLAG;
            else if (bitboardSquaresTraveled != 8 && !isOccupied(to))
                moveFlag = Move::EN_PASSANT_FLAG;
        }

        return Move(from, to, moveFlag);
    }

    inline void makeMove(const std::string uciMove) {
        makeMove(uciToMove(uciMove));
    }

    inline void makeMove(const Move move)
    {
        assert(mStates.size() >= 1 && mState == &mStates.back());
        const BoardState copy = *mState;
        mStates.push_back(copy);
        mState = &mStates.back();

        const Color oppSide = this->oppSide();
        mState->lastMove = move.encoded();

        if (move == MOVE_NONE) {
            assert(!inCheck());

            mState->colorToMove = oppSide;
            mState->zobristHash ^= ZOBRIST_COLOR;

            if (mState->enPassantSquare != SQUARE_NONE)
            {
                mState->zobristHash ^= ZOBRIST_FILES[(int)squareFile(mState->enPassantSquare)];
                mState->enPassantSquare = SQUARE_NONE;
            }

            mState->pliesSincePawnOrCapture++;

            if (sideToMove() == Color::WHITE)
                mState->currentMoveCounter++;

            mState->captured = PieceType::NONE;
            return;
        }

        const Square from = move.from();
        const Square to = move.to();
        const auto moveFlag = move.flag();
        const PieceType promotion = move.promotion();
        const PieceType pieceType = move.pieceType();
        Square capturedPieceSquare = to;

        removePiece(sideToMove(), pieceType, from);

        if (moveFlag == Move::CASTLING_FLAG)
        {
            placePiece(sideToMove(), PieceType::KING, to);
            const auto [rookFrom, rookTo] = CASTLING_ROOK_FROM_TO[to];
            removePiece(sideToMove(), PieceType::ROOK, rookFrom);
            placePiece(sideToMove(), PieceType::ROOK, rookTo);
            mState->captured = PieceType::NONE;
        }
        else if (moveFlag == Move::EN_PASSANT_FLAG)
        {
            capturedPieceSquare = sideToMove() == Color::WHITE ? to - 8 : to + 8;
            removePiece(oppSide, PieceType::PAWN, capturedPieceSquare);
            placePiece(sideToMove(), PieceType::PAWN, to);
            mState->captured = PieceType::PAWN;
        }
        else {
            if ((mState->captured = pieceTypeAt(to)) != PieceType::NONE)
                removePiece(oppSide, mState->captured, to);

            placePiece(sideToMove(), 
                       promotion != PieceType::NONE ? promotion : pieceType, 
                       to);
        }

        mState->zobristHash ^= mState->castlingRights; // XOR old castling rights out

        // Update castling rights
        if (pieceType == PieceType::KING)
        {
            mState->castlingRights &= ~CASTLING_MASKS[(int)sideToMove()][CASTLE_SHORT]; 
            mState->castlingRights &= ~CASTLING_MASKS[(int)sideToMove()][CASTLE_LONG]; 
        }
        else if (bitboard(from) & mState->castlingRights)
            mState->castlingRights &= ~bitboard(from);
        if (bitboard(to) & mState->castlingRights)
            mState->castlingRights &= ~bitboard(to);

        mState->zobristHash ^= mState->castlingRights; // XOR new castling rights in

        // Update en passant square
        if (mState->enPassantSquare != SQUARE_NONE)
        {
            mState->zobristHash ^= ZOBRIST_FILES[(int)squareFile(mState->enPassantSquare)];
            mState->enPassantSquare = SQUARE_NONE;
        }
        if (moveFlag == Move::PAWN_TWO_UP_FLAG)
        { 
            mState->enPassantSquare = sideToMove() == Color::WHITE ? to - 8 : to + 8;
            mState->zobristHash ^= ZOBRIST_FILES[(int)squareFile(mState->enPassantSquare)];
        }

        mState->colorToMove = oppSide;
        mState->zobristHash ^= ZOBRIST_COLOR;

        if (pieceType == PieceType::PAWN || mState->captured != PieceType::NONE)
            mState->pliesSincePawnOrCapture = 0;
        else
            mState->pliesSincePawnOrCapture++;

        if (sideToMove() == Color::WHITE)
            mState->currentMoveCounter++;

        mState->checkers = attackers(kingSquare()) & them();
    }

    inline void undoMove()
    {
        assert(mStates.size() >= 2 && mState == &mStates.back());
        mStates.pop_back();
        mState = &mStates.back();
    }

    inline void pseudolegalMoves(
        ArrayVec<Move, 256> &moves, const bool noisyOnly = false, const bool underpromotions = true) const
    {
        moves.clear();

        const Color enemyColor = oppSide();
        const u64 occ = occupancy();

        std::array<u64, 5> ourPiecesBbs = {
            getBb(sideToMove(), PieceType::PAWN),
            getBb(sideToMove(), PieceType::KNIGHT),
            getBb(sideToMove(), PieceType::BISHOP),
            getBb(sideToMove(), PieceType::ROOK),
            getBb(sideToMove(), PieceType::QUEEN)
        };

        // En passant
        if (mState->enPassantSquare != SQUARE_NONE)
        {   
            u64 ourEnPassantPawns = attacks::pawnAttacks(mState->enPassantSquare, enemyColor) & ourPiecesBbs[PAWN];

            while (ourEnPassantPawns > 0) {
                const Square ourPawnSquare = poplsb(ourEnPassantPawns);
                moves.push_back(Move(ourPawnSquare, mState->enPassantSquare, Move::EN_PASSANT_FLAG));
            }
        }

        while (ourPiecesBbs[PAWN] > 0) {
            const Square sq = poplsb(ourPiecesBbs[PAWN]);
            bool pawnHasntMoved = false, willPromote = false;
            const Rank rank = squareRank(sq);

            if (rank == Rank::RANK_2) {
                pawnHasntMoved = sideToMove() == Color::WHITE;
                willPromote = sideToMove() == Color::BLACK;
            } else if (rank == Rank::RANK_7) {
                pawnHasntMoved = sideToMove() == Color::BLACK;
                willPromote = sideToMove() == Color::WHITE;
            }

            // Generate this pawn's captures

            u64 pawnAttacks = attacks::pawnAttacks(sq, sideToMove()) & them();

            while (pawnAttacks > 0) {
                const Square targetSquare = poplsb(pawnAttacks);
                if (willPromote) 
                    addPromotions(moves, sq, targetSquare, underpromotions);
                else 
                    moves.push_back(Move(sq, targetSquare, Move::PAWN_FLAG));
            }

            const Square squareOneUp = sideToMove() == Color::WHITE ? sq + 8 : sq - 8;
            if (isOccupied(squareOneUp))
                continue;

            if (willPromote) {
                addPromotions(moves, sq, squareOneUp, underpromotions);
                continue;
            }

            if (noisyOnly) continue;

            // pawn 1 square up
            moves.push_back(Move(sq, squareOneUp, Move::PAWN_FLAG));

            // pawn 2 squares up
            const Square squareTwoUp = sideToMove() == Color::WHITE ? sq + 16 : sq - 16;
            if (pawnHasntMoved && !isOccupied(squareTwoUp))
                moves.push_back(Move(sq, squareTwoUp, Move::PAWN_TWO_UP_FLAG));
        }

        const u64 mask = noisyOnly ? them() : ~us();

        while (ourPiecesBbs[KNIGHT] > 0) {
            const Square sq = poplsb(ourPiecesBbs[KNIGHT]);
            u64 knightMoves = attacks::knightAttacks(sq) & mask;

            while (knightMoves > 0) {
                const Square targetSquare = poplsb(knightMoves);
                moves.push_back(Move(sq, targetSquare, Move::KNIGHT_FLAG));
            }
        }
        
        while (ourPiecesBbs[BISHOP] > 0) {
            const Square sq = poplsb(ourPiecesBbs[BISHOP]);
            u64 bishopMoves = attacks::bishopAttacks(sq, occ) & mask;

            while (bishopMoves > 0) {
                const Square targetSquare = poplsb(bishopMoves);
                moves.push_back(Move(sq, targetSquare, Move::BISHOP_FLAG));
            }
        }

        while (ourPiecesBbs[ROOK] > 0) {
            const Square sq = poplsb(ourPiecesBbs[ROOK]);
            u64 rookMoves = attacks::rookAttacks(sq, occ) & mask;

            while (rookMoves > 0) {
                const Square targetSquare = poplsb(rookMoves);
                moves.push_back(Move(sq, targetSquare, Move::ROOK_FLAG));
            }
        }

        while (ourPiecesBbs[QUEEN] > 0) {
            const Square sq = poplsb(ourPiecesBbs[QUEEN]);
            u64 queenMoves = attacks::queenAttacks(sq, occ) & mask;

            while (queenMoves > 0) {
                const Square targetSquare = poplsb(queenMoves);
                moves.push_back(Move(sq, targetSquare, Move::QUEEN_FLAG));
            }
        }

        const Square kingSquare = this->kingSquare();
        u64 kingMoves = attacks::kingAttacks(kingSquare) & mask;

        while (kingMoves > 0) {
            const Square targetSquare = poplsb(kingMoves);
            moves.push_back(Move(kingSquare, targetSquare, Move::KING_FLAG));
        }

        // Castling
        if (!noisyOnly && !inCheck())
        {
            // Short castle
            if ((mState->castlingRights & CASTLING_MASKS[(int)sideToMove()][CASTLE_SHORT])
            && !(occ & BETWEEN[kingSquare][kingSquare+3]))
                moves.push_back(Move(kingSquare, kingSquare + 2, Move::CASTLING_FLAG));

            // Long castle
            if ((mState->castlingRights & CASTLING_MASKS[(int)sideToMove()][CASTLE_LONG])
            && !(occ & BETWEEN[kingSquare][kingSquare-4]))
                moves.push_back(Move(kingSquare, kingSquare - 2, Move::CASTLING_FLAG));
        }
    }

    private:

    inline void addPromotions(
        ArrayVec<Move, 256> &moves, const Square sq, const Square targetSquare, const bool underpromotions) const
    {
        moves.push_back(Move(sq, targetSquare, Move::QUEEN_PROMOTION_FLAG));

        if (underpromotions) {
            moves.push_back(Move(sq, targetSquare, Move::ROOK_PROMOTION_FLAG));
            moves.push_back(Move(sq, targetSquare, Move::BISHOP_PROMOTION_FLAG));
            moves.push_back(Move(sq, targetSquare, Move::KNIGHT_PROMOTION_FLAG));
        }
    }

    public:

    inline bool isPseudolegalLegal(const Move move, const u64 pinned) const
    {
        const auto moveFlag = move.flag();
        const Square from = move.from();
        const Square to = move.to();
        const Color oppSide = this->oppSide();

        if (moveFlag == Move::CASTLING_FLAG)
            return to > from
                   // Short castle
                   ? !isSquareAttacked(from + 1, oppSide) && !isSquareAttacked(from + 2, oppSide)
                   // Long castle
                   : !isSquareAttacked(from - 1, oppSide) && !isSquareAttacked(from - 2, oppSide);

        const Square kingSquare = this->kingSquare();

        if (moveFlag == Move::EN_PASSANT_FLAG) 
        {
            const Square capturedSq = sideToMove() == Color::WHITE ? to - 8 : to + 8;
            const u64 occAfter = occupancy() ^ bitboard(from) ^ bitboard(capturedSq) ^ bitboard(to);

            const u64 bishopsQueens = getBb(PieceType::BISHOP) | getBb(PieceType::QUEEN);
            const u64 rooksQueens   = getBb(PieceType::ROOK)   | getBb(PieceType::QUEEN);

            u64 slidingAttackersTo = attacks::bishopAttacks(kingSquare, occAfter) & bishopsQueens;
            slidingAttackersTo    |= attacks::rookAttacks(kingSquare, occAfter)   & rooksQueens;

            return (slidingAttackersTo & them()) == 0;
        }

        if (move.pieceType() == PieceType::KING)
            return !isSquareAttacked(to, oppSide, occupancy() ^ bitboard(from));

        if (std::popcount(mState->checkers) > 1) 
            return false;

        if ((pinned & bitboard(from)) > 0 && (LINE_THROUGH[from][to] & bitboard(kingSquare)) == 0)
            return false;

        // 1 checker
        if (mState->checkers > 0) {
            Square checkerSquare = lsb(mState->checkers);
            return bitboard(to) & (BETWEEN[kingSquare][checkerSquare] | mState->checkers);
        }

        return true;
    }

    inline bool hasNonPawnMaterial(const Color color) const
    {
        return getBb(color, PieceType::KNIGHT) > 0
            || getBb(color, PieceType::BISHOP) > 0
            || getBb(color, PieceType::ROOK)   > 0
            || getBb(color, PieceType::QUEEN)  > 0;
    }

    inline u64 roughHashAfter(const Move move) const
    {
        u64 hashAfter = zobristHash() ^ ZOBRIST_COLOR;

        if (move == MOVE_NONE) return hashAfter;
        
        const int stm = (int)sideToMove();
        const Square to = move.to();
        const PieceType captured = pieceTypeAt(to);

        if (captured != PieceType::NONE)
            hashAfter ^= ZOBRIST_PIECES[!stm][(int)captured][to];

        const int pieceType = (int)move.pieceType();
        return hashAfter ^ ZOBRIST_PIECES[stm][pieceType][move.from()] ^ ZOBRIST_PIECES[stm][pieceType][to];
    }

    // Cuckoo / detect upcoming repetition
    inline bool hasUpcomingRepetition(const int ply) const {
        //int stateIdxAfterPawnOrCapture = std::max(0, (int)mStates.size() - (int)mState->pliesSincePawnOrCapture - 1);
        //int rootStateIdx = (int)mStates.size() - ply - 1;

        const int end = std::min((int)mState->pliesSincePawnOrCapture, (int)mStates.size() - 1);
        if (end < 3) return false;

        const u64 occ = occupancy();

        for (int i = 3; i <= end; i += 2)
        {
            assert((int)mStates.size() - 1 - i >= 0);
            const u64 moveKey = zobristHash() ^ mStates[(int)mStates.size() - 1 - i].zobristHash;

            int cuckooIdx;

            if (cuckoo::KEYS[cuckooIdx = cuckoo::h1(moveKey)] == moveKey 
            ||  cuckoo::KEYS[cuckooIdx = cuckoo::h2(moveKey)] == moveKey)
            {
                const Move move = cuckoo::MOVES[cuckooIdx];
                const Square from = move.from();
                const Square to = move.to();

                if (((BETWEEN[from][to] | bitboard(to)) ^ bitboard(to)) & occ)
                    continue;

                if (ply > i) return true; // Repetition after root

                const Color pieceColor = getBb(Color::WHITE) & (occ & bitboard(from) ? bitboard(from) : bitboard(to)) 
                                         ? Color::WHITE : Color::BLACK;

                if (pieceColor != sideToMove()) continue;

                // Require one more repetition at and before root
                for (int j = i + 4; j <= end; j += 2)
                    if (mStates[(int)mStates.size() - 1 - i].zobristHash == mStates[(int)mStates.size() - 1 - j].zobristHash)
                        return true;
            }
        }

        return false;
    }

    inline bool hasLegalMove()
    {   
        const int stm = (int)sideToMove();
        const Color enemyColor = oppSide();

        const u64 occ = occupancy();
        const Square kingSquare = this->kingSquare();
        const u64 theirAttacks = attacks(enemyColor, occ ^ bitboard(kingSquare));

        // Does king have legal move?

        const u64 targetSquares = attacks::kingAttacks(kingSquare) & ~us() & ~theirAttacks;

        if (targetSquares > 0) return true;

        const int numCheckers = std::popcount(mState->checkers);
        assert(numCheckers <= 2);

        // If in double check, only king moves are allowed
        if (numCheckers > 1) return false;

        u64 movableBb = ONES;
        
        if (numCheckers == 1) {
            movableBb = mState->checkers;

            if (mState->checkers & (getBb(PieceType::BISHOP) | getBb(PieceType::ROOK) | getBb(PieceType::QUEEN)))
            {
                const Square checkerSquare = lsb(mState->checkers);
                movableBb |= BETWEEN[kingSquare][checkerSquare];
            }
        }

        // Castling
        if (numCheckers == 0)
        {
            if (mState->castlingRights & CASTLING_MASKS[stm][CASTLE_SHORT]) 
            {
                const u64 throughSquares = bitboard(kingSquare + 1) | bitboard(kingSquare + 2);

                if ((occ & throughSquares) == 0 && (theirAttacks & throughSquares) == 0)
                    return true;
            }

            if (mState->castlingRights & CASTLING_MASKS[stm][CASTLE_LONG]) 
            {
                const u64 throughSquares = bitboard(kingSquare - 1) | bitboard(kingSquare - 2) | bitboard(kingSquare - 3);

                if ((occ & throughSquares) == 0 
                && (theirAttacks & (throughSquares ^ bitboard(kingSquare - 3))) == 0)
                    return true;
            }
        }

        // Get pinnedNonDiagonal

        u64 pinnedNonDiagonal = 0;

        const u64 rookAttacks = attacks::rookAttacks(kingSquare, occ);
        const u64 xrayRook = rookAttacks ^ attacks::rookAttacks(kingSquare, occ ^ (us() & rookAttacks));

        u64 pinnersNonDiagonal = getBb(PieceType::ROOK) | getBb(PieceType::QUEEN);
        pinnersNonDiagonal &= xrayRook & them();
        
        while (pinnersNonDiagonal) {
            const Square pinnerSquare = poplsb(pinnersNonDiagonal);
            pinnedNonDiagonal |= BETWEEN[pinnerSquare][kingSquare] & us();
        }

        // Get pinnedDiagonal

        u64 pinnedDiagonal = 0;

        const u64 bishopAttacks = attacks::bishopAttacks(kingSquare, occ);
        const u64 xrayBishop = bishopAttacks ^ attacks::bishopAttacks(kingSquare, occ ^ (us() & bishopAttacks));

        u64 pinnersDiagonal = getBb(PieceType::BISHOP) | getBb(PieceType::QUEEN);
        pinnersDiagonal &= xrayBishop & them();

        while (pinnersDiagonal) {
            const Square pinnerSquare = poplsb(pinnersDiagonal);
            pinnedDiagonal |= BETWEEN[pinnerSquare][kingSquare] & us();
        }

        // Check if other pieces have a legal move (not king)

        // [pieceType]
        std::array<u64, 5> ourPieces = {
            getBb(sideToMove(), PieceType::PAWN),
            getBb(sideToMove(), PieceType::KNIGHT) & ~pinnedDiagonal & ~pinnedNonDiagonal,
            getBb(sideToMove(), PieceType::BISHOP) & ~pinnedNonDiagonal,
            getBb(sideToMove(), PieceType::ROOK)   & ~pinnedDiagonal,
            getBb(sideToMove(), PieceType::QUEEN)
        };

        while (ourPieces[KNIGHT] > 0) {
            const Square sq = poplsb(ourPieces[KNIGHT]);
            const u64 knightMoves = attacks::knightAttacks(sq) & ~us() & movableBb;
            if (knightMoves > 0) return true;
        }
        
        while (ourPieces[BISHOP] > 0) {
            const Square sq = poplsb(ourPieces[BISHOP]);
            u64 bishopMoves = attacks::bishopAttacks(sq, occ) & ~us() & movableBb;

            if (bitboard(sq) & pinnedDiagonal)
                bishopMoves &= LINE_THROUGH[kingSquare][sq];

            if (bishopMoves > 0) return true;
        }

        while (ourPieces[ROOK] > 0) {
            const Square sq = poplsb(ourPieces[ROOK]);
            u64 rookMoves = attacks::rookAttacks(sq, occ) & ~us() & movableBb;

            if (bitboard(sq) & pinnedNonDiagonal)
                rookMoves &= LINE_THROUGH[kingSquare][sq];

            if (rookMoves > 0) return true;
        }

        while (ourPieces[QUEEN] > 0) {
            const Square sq = poplsb(ourPieces[QUEEN]);
            u64 queenMoves = attacks::queenAttacks(sq, occ) & ~us() & movableBb;

            if (bitboard(sq) & (pinnedDiagonal | pinnedNonDiagonal))
                queenMoves &= LINE_THROUGH[kingSquare][sq];

            if (queenMoves > 0) return true;
        }
        
        // En passant
        if (mState->enPassantSquare != SQUARE_NONE)
        {
            u64 ourNearbyPawns = ourPieces[PAWN] & attacks::pawnAttacks(mState->enPassantSquare, enemyColor);
            
            while (ourNearbyPawns) {
                const Square ourPawnSquare = poplsb(ourNearbyPawns);

                const auto colorBitboards = mState->colorBitboards;
                const auto pawnBb = mState->piecesBitboards[PAWN];
                const auto zobristHash = mState->zobristHash;
                const auto pawnsHash = mState->pawnsHash;

                // Make the en passant move

                removePiece(sideToMove(), PieceType::PAWN, ourPawnSquare);
                placePiece(sideToMove(), PieceType::PAWN, mState->enPassantSquare);

                const Square capturedPawnSquare = sideToMove() == Color::WHITE
                                                  ? mState->enPassantSquare - 8 : mState->enPassantSquare + 8;

                removePiece(enemyColor, PieceType::PAWN, capturedPawnSquare);

                const bool enPassantLegal = !isSquareAttacked(kingSquare, enemyColor);

                // Undo the en passant move
                mState->colorBitboards = colorBitboards;
                mState->piecesBitboards[PAWN] = pawnBb;
                mState->zobristHash = zobristHash;
                mState->pawnsHash = pawnsHash;

                if (enPassantLegal) return true;
            }
        }

        while (ourPieces[PAWN] > 0) {
            const Square sq = poplsb(ourPieces[PAWN]);

            // Pawn's captures

            u64 pawnAttacks = attacks::pawnAttacks(sq, sideToMove()) & them() & movableBb;

            if (bitboard(sq) & (pinnedDiagonal | pinnedNonDiagonal)) 
                pawnAttacks &= LINE_THROUGH[kingSquare][sq];

            if (pawnAttacks > 0) return true;

            // Pawn single push

            if (bitboard(sq) & pinnedDiagonal) continue;

            const u64 pinRay = LINE_THROUGH[sq][kingSquare];
            const bool pinnedHorizontally = (bitboard(sq) & pinnedNonDiagonal) > 0 && (pinRay & (pinRay << 1)) > 0;

            if (pinnedHorizontally) continue;

            const Square squareOneUp = sideToMove() == Color::WHITE ? sq + 8 : sq - 8;

            if (isOccupied(squareOneUp)) continue;

            if (movableBb & bitboard(squareOneUp))
                return true;

            // Pawn double push
            if (squareRank(sq) == (sideToMove() == Color::WHITE ? Rank::RANK_2 : Rank::RANK_7))
            {
                const Square squareTwoUp = sideToMove() == Color::WHITE ? sq + 16 : sq - 16;

                if ((movableBb & bitboard(squareTwoUp)) && !isOccupied(squareTwoUp))
                    return true;
            }
        }

        return false;
    }

}; // class Board
