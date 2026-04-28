#include "search.h"
#include "movegen.h"
#include "evaluate.h"
#include "tt.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

// ============================================================
// LMR reduction table: reduction[depth][move_count]
// ============================================================

static int LMR[64][64];

static void initLMR() {
    for (int d = 1; d < 64; d++)
        for (int m = 1; m < 64; m++)
            LMR[d][m] = int(0.75 + std::log(d) * std::log(m) / 2.25);
}

// ============================================================
// Aspiration window constants
// ============================================================

static constexpr int ASPIRATION_DELTA   = 25;
static constexpr int ASPIRATION_MAX     = 500;

// ============================================================
// Pruning margins
// ============================================================

// Reverse futility: static_eval - margin > beta → prune
static int rfpMargin(int depth) { return 80 * depth; }

// Futility: static_eval + margin < alpha → skip quiets
static int futilityMargin(int depth) { return 80 * depth; }

// ============================================================
// Static eval (NNUE if loaded, else HCE)
// ============================================================

int SearchThread::staticEval(const Position& pos, SearchStack* ss) {
    int e = evaluate(pos);
    ss->staticEval = e;
    return e;
}

// ============================================================
// Quiescence search
// ============================================================

int SearchThread::qsearch(Position& pos, int alpha, int beta, int ply, SearchStack* ss) {
    nodes_++;

    if (ply >= SearchStack::MAX_PLY - 1) return evaluate(pos);
    if (pos.isDraw()) return VALUE_DRAW;

    // TT lookup
    int ttScore, ttEval, ttDepth;
    Move ttMove;
    TTBound ttBound;
    bool ttHit = TT.probe(pos.key(), ply, ttScore, ttEval, ttMove, ttDepth, ttBound);

    if (ttHit) {
        if (ttBound == BOUND_EXACT)                return ttScore;
        if (ttBound == BOUND_LOWER && ttScore >= beta)  return ttScore;
        if (ttBound == BOUND_UPPER && ttScore <= alpha) return ttScore;
    }

    // Standing pat
    int eval = ss->staticEval != VALUE_NONE ? ss->staticEval : staticEval(pos, ss);
    if (ttHit && ttEval != VALUE_NONE) eval = ttEval;

    if (eval >= beta) return eval;
    int bestScore = eval;
    if (eval > alpha) alpha = eval;

    // Delta pruning threshold
    int deltaThreshold = alpha - eval - 200;

    Move bestMove = Move::none();
    int  originalAlpha = alpha;

    MovePicker mp(pos, ttMove, 0);
    Move m;
    StateInfo si;

    while ((m = mp.next()) != Move::none()) {
        if (!pos.isLegal(m)) continue;

        // Delta pruning: skip captures that can't raise alpha
        if (m.isCapture() && !m.isPromotion()
            && bestScore > -MATE_IN_MAX_PLY
            && PieceValue[typeOf(m.captured())] < deltaThreshold)
            continue;

        // SEE pruning for losing captures
        if (m.isCapture() && !pos.seeGe(m, -50)) continue;

        (ss+1)->staticEval = VALUE_NONE;
        pos.makeMove(m, si);

        int score = -qsearch(pos, -beta, -alpha, ply + 1, ss + 1);
        pos.unmakeMove(m);

        if (shouldStop()) return bestScore;

        if (score > bestScore) {
            bestScore = score;
            bestMove  = m;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    TT.store(pos.key(), ply, bestScore, eval, bestMove, 0, BOUND_LOWER);
                    return bestScore;
                }
            }
        }
    }

    TTBound bound = (bestScore >= beta) ? BOUND_LOWER
                  : (bestScore > originalAlpha) ? BOUND_EXACT : BOUND_UPPER;
    TT.store(pos.key(), ply, bestScore, eval, bestMove, 0, bound);
    return bestScore;
}

// ============================================================
// Negamax alpha-beta
// ============================================================

int SearchThread::negamax(Position& pos, int alpha, int beta, int depth,
                           int ply, SearchStack* ss, bool cutNode) {
    bool pvNode = (beta - alpha > 1);
    bool root   = (ply == 0);

    pvLength_[ply] = ply;

    // Quiescence at leaf
    if (depth <= 0) return qsearch(pos, alpha, beta, ply, ss);

    nodes_++;

    // Check draw / max ply
    if (!root && pos.isDraw()) return VALUE_DRAW;
    if (ply >= SearchStack::MAX_PLY - 1) return evaluate(pos);

    // ---- TT probe ----
    int ttScore, ttEval, ttDepth;
    Move ttMove;
    TTBound ttBound;
    bool ttHit = TT.probe(pos.key(), ply, ttScore, ttEval, ttMove, ttDepth, ttBound);

    // TT cutoff (not at root or PV)
    if (!pvNode && ttHit && ttDepth >= depth) {
        if (ttBound == BOUND_EXACT) return ttScore;
        if (ttBound == BOUND_LOWER && ttScore >= beta)  return ttScore;
        if (ttBound == BOUND_UPPER && ttScore <= alpha) return ttScore;
    }

    // ---- In check? ----
    ss->inCheck = pos.isInCheck();
    if (ss->inCheck) depth++; // check extension

    // ---- Static eval ----
    int eval;
    if (ss->inCheck) {
        eval = ss->staticEval = VALUE_NONE;
    } else {
        if (ttHit && ttEval != VALUE_NONE) {
            eval = ss->staticEval = ttEval;
        } else {
            eval = staticEval(pos, ss);
        }
        // Use TT score as better eval estimate
        if (ttHit) {
            if (ttBound == BOUND_EXACT)                     eval = ttScore;
            else if (ttBound == BOUND_LOWER && ttScore > eval) eval = ttScore;
            else if (ttBound == BOUND_UPPER && ttScore < eval) eval = ttScore;
        }
    }

    bool improving = !ss->inCheck && ply >= 2
                   && (ss-2)->staticEval != VALUE_NONE
                   && eval > (ss-2)->staticEval;

    // ---- Pruning (not in check, not PV) ----
    if (!pvNode && !ss->inCheck && ss->excludedMove.isNone()) {

        // Reverse Futility Pruning
        if (depth <= 8 && eval - rfpMargin(depth) >= beta && std::abs(eval) < MATE_IN_MAX_PLY)
            return eval;

        // Null Move Pruning
        if (depth >= 3 && eval >= beta
            && pos.nonPawnMaterial(pos.sideToMove()) > 0
            && ss->currentMove != Move::null()
            && pos.pliesFromNull() > 0) {

            int R = 3 + depth / 4 + std::min((eval - beta) / 150, 3);
            StateInfo nullSi;
            pos.makeNullMove(nullSi);
            (ss+1)->staticEval = VALUE_NONE;
            int nullScore = -negamax(pos, -beta, -beta + 1, depth - R, ply + 1, ss + 1, !cutNode);
            pos.unmakeNullMove();

            if (nullScore >= beta && std::abs(nullScore) < MATE_IN_MAX_PLY)
                return nullScore;
        }

        // Razoring (depth == 1)
        if (depth == 1 && eval + 300 < alpha)
            return qsearch(pos, alpha, beta, ply, ss);
    }

    // ---- Move loop ----
    Move killer1  = history.killers.get(ply, 0);
    Move killer2  = history.killers.get(ply, 1);
    Move prevMove = (ply > 0) ? (ss-1)->currentMove : Move::none();
    Move counter  = (ply > 0 && !prevMove.isNone() && !prevMove.isNull())
                  ? history.getCounter(prevMove.piece(), prevMove.to())
                  : Move::none();

    MovePicker mp(pos, ttMove, killer1, killer2, counter, history, depth);

    Move bestMove  = Move::none();
    int  bestScore = -VALUE_INFINITE;
    int  moveCount = 0;
    int  originalAlpha = alpha;
    StateInfo si;

    // Quiet moves tried (for history update on cutoff)
    static constexpr int MAX_MOVES_TRACK = 64;
    Move quietsTried[MAX_MOVES_TRACK];
    Move capturesTried[MAX_MOVES_TRACK];
    int  quietCount = 0, captureCount = 0;

    Move m;
    while ((m = mp.next()) != Move::none()) {
        if (m == ss->excludedMove) continue;
        if (!pos.isLegal(m)) continue;

        moveCount++;
        bool isCapture  = m.isCapture();
        bool isPromotion = m.isPromotion();

        // ---- Late Move Pruning ----
        if (!pvNode && !ss->inCheck && depth <= 8 && !isCapture && !isPromotion
            && bestScore > -MATE_IN_MAX_PLY) {
            int lmp_threshold = (3 + depth * depth) * (improving ? 2 : 1);
            if (moveCount > lmp_threshold) {
                mp.next(); // skip remaining quiets
                break;
            }
        }

        // ---- Futility pruning ----
        if (!pvNode && !ss->inCheck && depth <= 6 && !isCapture && !isPromotion
            && ss->staticEval != VALUE_NONE
            && ss->staticEval + futilityMargin(depth) < alpha
            && bestScore > -MATE_IN_MAX_PLY)
            continue;

        // ---- SEE pruning for bad captures / losing moves ----
        if (!pvNode && !ss->inCheck && moveCount > 1 && depth <= 8) {
            int seeThreshold = isCapture ? (-20 * depth * depth) : (-70 * depth);
            if (!pos.seeGe(m, seeThreshold)) continue;
        }

        // ---- Extension ----
        int extension = 0;

        // Singular extension: TT move at non-root with significant margin
        if (!root && depth >= 8 && m == ttMove && !ss->excludedMove.isNone() == false
            && ttDepth >= depth - 3 && ttBound != BOUND_UPPER
            && std::abs(ttScore) < MATE_IN_MAX_PLY) {
            int singularBeta = ttScore - 2 * depth;
            int singularDepth = (depth - 1) / 2;
            ss->excludedMove = m;
            int singularScore = negamax(pos, singularBeta - 1, singularBeta,
                                         singularDepth, ply, ss, cutNode);
            ss->excludedMove = Move::none();
            if (singularScore < singularBeta)
                extension = 1;
            else if (singularBeta >= beta)
                return singularBeta; // multi-cut pruning
        }

        // Make move
        ss->currentMove = m;
        (ss+1)->staticEval = VALUE_NONE;
        (ss+1)->ttPv = pvNode || (ttHit && (ss)->ttPv);
        pos.makeMove(m, si);

        // Track moves tried
        if (isCapture) {
            if (captureCount < MAX_MOVES_TRACK) capturesTried[captureCount++] = m;
        } else {
            if (quietCount < MAX_MOVES_TRACK) quietsTried[quietCount++] = m;
        }

        int newDepth = depth - 1 + extension;
        int score;

        // ---- Late Move Reductions ----
        if (depth >= 2 && moveCount > (pvNode ? 5 : 3) && !isCapture && !isPromotion) {
            int R = LMR[std::min(63, depth)][std::min(63, moveCount)];

            // Adjust reduction
            R -= pvNode ? 1 : 0;
            R -= ss->inCheck ? 1 : 0;
            R += cutNode ? 1 : 0;
            R -= improving ? 1 : 0;

            // History-based adjustment
            int histScore = history.getHistory(pos.sideToMove(), m.from(), m.to());
            R -= std::clamp(histScore / 8192, -2, 2);

            R = std::clamp(R, 0, newDepth - 1);

            // Reduced-depth search
            score = -negamax(pos, -alpha - 1, -alpha, newDepth - R, ply + 1, ss + 1, true);

            // Re-search at full depth if LMR found something interesting
            if (score > alpha && R > 0)
                score = -negamax(pos, -alpha - 1, -alpha, newDepth, ply + 1, ss + 1, !cutNode);
        } else if (!pvNode || moveCount > 1) {
            // Full-window search for remaining non-PV moves
            score = -negamax(pos, -alpha - 1, -alpha, newDepth, ply + 1, ss + 1, !cutNode);
        } else {
            score = -VALUE_INFINITE; // Will trigger PV search below
        }

        // PV search: full window for first move or re-search
        if (pvNode && (moveCount == 1 || score > alpha)) {
            score = -negamax(pos, -beta, -alpha, newDepth, ply + 1, ss + 1, false);
        }

        pos.unmakeMove(m);

        if (shouldStop()) return bestScore;

        if (score > bestScore) {
            bestScore = score;
            bestMove  = m;

            if (score > alpha) {
                alpha = score;
                if (pvNode) updatePV(m, ply);

                if (score >= beta) {
                    // Beta cutoff — update history
                    updateHistories(pos, m, depth, ply, ss,
                                    quietsTried, quietCount - (isCapture ? 0 : 1),
                                    capturesTried, captureCount - (isCapture ? 1 : 0));
                    TT.store(pos.key(), ply, bestScore, ss->staticEval,
                             bestMove, depth, BOUND_LOWER);
                    return bestScore;
                }
            }
        }
    }

    // ---- Checkmate / stalemate ----
    if (moveCount == 0) {
        if (ss->inCheck) return -VALUE_MATE + ply;
        return VALUE_DRAW;
    }

    TTBound bound = (bestScore >= beta) ? BOUND_LOWER
                  : (bestScore > originalAlpha) ? BOUND_EXACT : BOUND_UPPER;
    TT.store(pos.key(), ply, bestScore, ss->staticEval, bestMove, depth, bound);
    return bestScore;
}

// ============================================================
// History update on cutoff
// ============================================================

void SearchThread::updateHistories(Position& pos, Move bestMove, int depth, int ply,
                                    SearchStack* ss, Move* quiets, int quietCount,
                                    Move* captures, int captureCount) {
    int bonus = std::min(depth * depth, 2048);
    int malus = -bonus;

    Color us = pos.sideToMove();

    Move prevMove1 = (ply > 0) ? (ss-1)->currentMove : Move::none();
    Move prevMove2 = (ply > 1) ? (ss-2)->currentMove : Move::none();

    if (pos.isCapture(bestMove)) {
        // Bonus for best capture
        PieceType captured = typeOf(pos.pieceOn(bestMove.to()));
        history.updateCapture(pos.pieceOn(bestMove.from()), bestMove.to(), captured, bonus);
        // Malus for captures that failed to cut off
        for (int i = 0; i < captureCount; i++) {
            Move m = captures[i];
            if (m == bestMove) continue;
            PieceType cap = typeOf(pos.pieceOn(m.to()));
            history.updateCapture(pos.pieceOn(m.from()), m.to(), cap, malus);
        }
    } else {
        // Bonus for best quiet move
        history.updateQuiet(us, bestMove.from(), bestMove.to(),
                            bestMove.piece(), prevMove1, prevMove2, bonus);
        // Malus for quiet moves that failed
        for (int i = 0; i < quietCount; i++) {
            Move m = quiets[i];
            if (m == bestMove) continue;
            history.updateQuiet(us, m.from(), m.to(), m.piece(), prevMove1, prevMove2, malus);
        }
        history.updateKiller(ply, bestMove);
        if (ply > 0 && !prevMove1.isNone() && !prevMove1.isNull())
            history.updateCounter(prevMove1.piece(), prevMove1.to(), bestMove);
    }
}

// ============================================================
// Iterative deepening
// ============================================================

SearchResult SearchThread::search(Position& pos, const SearchLimits& limits,
                                   const TimeManager& tm, std::atomic<bool>& stop) {
    stop_  = &stop;
    tm_    = &tm;
    nodes_ = 0;

    clearPV();

    SearchStack stack[SearchStack::MAX_PLY + 4];
    SearchStack* ss = stack + 2;  // 2 entries padding for ply-2 lookups

    for (int i = -2; i < SearchStack::MAX_PLY + 2; i++) {
        (ss+i)->ply = i;
        (ss+i)->currentMove = Move::none();
        (ss+i)->excludedMove = Move::none();
        (ss+i)->staticEval = VALUE_NONE;
        (ss+i)->inCheck = false;
        (ss+i)->ttPv = false;
    }

    int maxDepth = (limits.depth > 0) ? limits.depth : SearchStack::MAX_PLY - 1;
    SearchResult result;

    int  prevScore = VALUE_NONE;
    Move bestMove  = Move::none();

    for (int depth = 1; depth <= maxDepth; depth++) {
        int score;

        if (depth < 5) {
            // No aspiration window for shallow depths
            score = negamax(pos, -VALUE_INFINITE, VALUE_INFINITE, depth, 0, ss, false);
        } else {
            // Aspiration windows
            int delta = ASPIRATION_DELTA;
            int alpha = std::max(-VALUE_INFINITE, prevScore - delta);
            int beta  = std::min( VALUE_INFINITE, prevScore + delta);

            while (true) {
                score = negamax(pos, alpha, beta, depth, 0, ss, false);

                if (shouldStop()) break;

                if (score <= alpha) {
                    alpha = std::max(-VALUE_INFINITE, alpha - delta);
                    delta += delta / 2;
                } else if (score >= beta) {
                    beta = std::min(VALUE_INFINITE, beta + delta);
                    delta += delta / 2;
                    if (delta > ASPIRATION_MAX) {
                        alpha = -VALUE_INFINITE;
                        beta  =  VALUE_INFINITE;
                    }
                } else {
                    break;
                }
            }
        }

        if (shouldStop() && depth > 1) break;

        prevScore = score;
        bestMove  = pvTable_[0][0];

        // Print UCI info
        if (id_ == 0) {
            Search::printInfo(depth, score, nodes_, tm.elapsed(),
                              TT.hashFull(), pvTable_[0], pvLength_[0]);
        }

        result.depth     = depth;
        result.score     = score;
        result.bestMove  = bestMove;
        result.ponderMove = (pvLength_[0] > 1) ? pvTable_[0][1] : Move::none();

        // Time check after each depth
        if (id_ == 0 && tm.shouldStopOptimal() && depth >= 4) break;
    }

    return result;
}

// ============================================================
// Global search state
// ============================================================

namespace Search {

static std::vector<SearchThread> threads_;
static std::atomic<bool> stopFlag_{false};
static SearchThread mainThread_;

void init() {
    initLMR();
    TT.resize(64); // default 64MB
    mainThread_.init(0);
}

void resizeTT(size_t mb) {
    TT.resize(mb);
}

void clearHistory() {
    mainThread_.history.clear();
    for (auto& t : threads_) t.history.clear();
}

void printInfo(int depth, int score, int64_t nodes, int64_t ms, int hashfull,
               const Move* pv, int pvLen) {
    std::cout << "info depth " << depth;

    if (std::abs(score) >= MATE_IN_MAX_PLY) {
        int mateIn = (score > 0)
                   ? (VALUE_MATE - score + 1) / 2
                   : -(VALUE_MATE + score) / 2;
        std::cout << " score mate " << mateIn;
    } else {
        std::cout << " score cp " << score;
    }

    int64_t nps = ms > 0 ? nodes * 1000 / ms : 0;
    std::cout << " nodes " << nodes
              << " nps "   << nps
              << " time "  << ms
              << " hashfull " << hashfull;

    if (pvLen > 0) {
        std::cout << " pv";
        for (int i = 0; i < pvLen; i++)
            std::cout << " " << moveName(pv[i]);
    }
    std::cout << "\n";
    std::cout.flush();
}

SearchResult go(Position& pos, const SearchLimits& limits) {
    stopFlag_.store(false);

    TT.newSearch();

    TimeManager tm;
    tm.infinite = limits.infinite || limits.ponder;
    if (!tm.infinite) {
        if (limits.movetime > 0) {
            tm.init(0, 0, 0, 0, 0, pos.sideToMove() == WHITE, limits.movetime);
        } else {
            tm.init(limits.wtime, limits.btime, limits.winc, limits.binc,
                    limits.movestogo, pos.sideToMove() == WHITE, 0);
        }
    }
    tm.startTime = Clock::now();

    return mainThread_.search(pos, limits, tm, stopFlag_);
}

void stop() {
    stopFlag_.store(true);
}

} // namespace Search

