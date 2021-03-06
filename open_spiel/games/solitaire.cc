#include "solitaire.h"

#include <deque>
#include <random>
#include <algorithm>
#include <optional>
#include <utility>
#include <math.h>

#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel_utils.h"

#define RESET   "\033[0m"
#define BLACK   "\033[30m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"


/* OPTIMIZATION NOTES

 The biggest issues are FindLocation and LegalChildren. FindLocation is slow because
 it iterates through all cards in the state and checks for equality. It'd probably be faster to maintain
 a map of cards to their current location in the state. Then we would only have to search a portion of the state
 to find the card. Or we could map the card directly to its pile and then use the pile type to get location.

 LegalChildren could be faster by simply storing the rank and suit of child cards given the location. This
 should be faster than having to look up the index of rank and suit.

- LegalActions                   42%
  - CandidateMoves               89%
    - FindLocation               44%
      - std::find                 73%
    - LegalChildren              18%
    - std::find                   12%
    - SolitaireState::Sources    10%
    - SolitaireState::Targets     8%

Most of the time spent is in LegalActions, which is what is broken down above.

- ApplyAction                    27%
  - DoApplyAction               100%
    - LegalActions               86%

The problem with this is that it has to repeatedly type cast to int which recomputes the card index from its
rank and suit. Once a card has been revealed though, its index never changes. Better to store it after its computed
the first time.

- ObservationTensor              24%
  - ToCardIndices                86%
    - Card::operator int         72%
      - GetIndex                 35%
*/

/* TODO LIST
- [√] Hidden cards shouldn't show up as sources or target.
- [~] Can't move a source that isn't the top card of its pile to the foundation
- [ ] ToString() methods shouldn't include padding. Their callers should handle how the strings are formatted.
- [ ] Create methods that allow casting to std::string for card, deck, waste, foundation, and tableau.
- [ ] Mask actions that move a pile starting with a king to an empty tableau.
- [√] Add support for serializing and deserializing state (update: I think the base implementations work fine)
- [ ] Wherever you iterate over cards in a pile, make sure it's not empty
- [ ] Have an option to turn off colors in strings and std::cout
- [√] Maybe another way to check if a state is terminal is see if the last 8 actions are draws
- [ ] Issue with reversible moves, can't move a card to another pile to reveal a card to be moved to the foundations
- [ ] After a chance move, you should be able to play a reversible move again, even if the players last move was reversible
  (i.e. chance/kReveal moves are irreversible) 
- [ ] After draw_counter reaches its limit, kDraw should be eliminated as a LegalAction. Then always allow reversible
  moves that target a foundation card.
- [ ] Add exceptions
- [ ] Maybe some constructors should use optional instead of just overloading
 */


namespace open_spiel::solitaire {

    namespace {
        const GameType kGameType {
                "solitaire",
                "Solitaire",
                GameType::Dynamics::kSequential,
                GameType::ChanceMode::kExplicitStochastic,
                GameType::Information::kImperfectInformation,
                GameType::Utility::kGeneralSum,
                GameType::RewardModel::kRewards,
                1,
                1,
                true,
                true,
                true,
                true,
                {{"players", GameParameter(kDefaultPlayers)}}
        };

        std::shared_ptr<const Game> Factory(const GameParameters & params) {
            return std::shared_ptr<const Game>(new SolitaireGame(params));
        }

        REGISTER_SPIEL_GAME(kGameType, Factory)
    }

    // Flags ===========================================================================================================

    bool LOG_FLAG = false;

    // Miscellaneous Functions =========================================================================================

    void log(const std::string & text) {
        if (LOG_FLAG) {
            std::cout << CYAN << "LOG : " << text << RESET << std::endl;
        }
    }

    std::vector<std::string> GetOppositeSuits(const std::string & suit) {
        log("Entering GetOppositeSuits()");

        if (suit == "s" or suit == "c") {
            return {"h", "d"};
        } else if (suit == "h" or suit == "d") {
            return {"s", "c"};
        } else {
            std::cout << YELLOW << "WARNING: `suit` is not in (s, h, c, d)" << RESET << std::endl;
        }

    }

    std::string LocationString(Location location) {
        log("Entering LocationString()");

        switch (location) {
            case kDeck : {
                return "kDeck";
            }
            case kWaste : {
                return "kWaste";
            }
            case kFoundation : {
                return "kFoundation";
            }
            case kTableau : {
                return "kTableau";
            }
            case kMissing : {
                return "kMissing";
            }
            default : {
                return "n/a";
            }
        }
    }

    std::vector<double> ToCardIndices(const std::deque<Card> & pile, int length) {
        // TODO: Handle empty piles

        log("Entering ToCardIndices()");
        std::vector<double> index_vector;
        for (auto & card : pile) {
            if (card.hidden) {
                index_vector.push_back(HIDDEN_CARD);
            } else {
                index_vector.push_back((int) card);
            }
        }
        index_vector.resize(length, NO_CARD);
        return index_vector;

    }

    // Card Methods ====================================================================================================

    Card::Card(std::string rank, std::string suit) :
        rank(std::move(rank)),
        suit(std::move(suit)),
        hidden(true),
        location(kMissing) {
        log("Entering Card(std::string rank, std::string suit)");
    }

    Card::Card() : rank(""), suit(""), hidden(true), location(kMissing) {
        log("Entering Card()");
    }

    Card::Card(int index) : hidden(false), location(kMissing) {
        log("Entering Card(int index)");

        // Handles special cards
        if (index < 0) {
            rank = "";
            switch (index) {
                case -1 : { suit = "s"; break; }
                case -2 : { suit = "h"; break; }
                case -3 : { suit = "c"; break; }
                case -4 : { suit = "d"; break; }
                case -5 : { suit =  ""; break; }
                default : {
                    std::cout << YELLOW << "WARNING: Incorrect index for special card";
                    break;
                }
            }
        }
        // Handles ordinary cards
        else {
            int rank_value = index % 13;
            int suit_value = floor(index / 13);
            rank = RANKS.at(rank_value);
            suit = SUITS.at(suit_value);
        }


    }

    Card::operator int() const {
        // OPTIMIZE

        log("Entering Card::operator int()");

        // Handles special cards
        if (rank.empty()) {
            if      (suit == "s")  { return -1; }
            else if (suit == "h")  { return -2; }
            else if (suit == "c")  { return -3; }
            else if (suit == "d")  { return -4; }
            else if (suit.empty()) { return -5; }
        }
        // Handles ordinary cards
        else {
            int rank_value = GetIndex(RANKS, rank);
            int suit_value = GetIndex(SUITS, suit);
            return 13 * suit_value + rank_value;
        }
    }

    bool Card::operator==(Card & other_card) const {
        // log("Entering Card::operator==");

        return rank == other_card.rank and suit == other_card.suit;
    }

    bool Card::operator==(const Card & other_card) const {
        // log("Entering Card::operator==");

        return rank == other_card.rank and suit == other_card.suit;
    }

    std::vector<Card> Card::LegalChildren() const {
        log("Entering LegalChildren");

        std::vector<Card>        legal_children = {};
        std::string              child_rank;
        std::vector<std::string> child_suits;

        // A hidden card has no legal children
        if (hidden) {
            return legal_children;
        }

        switch (location) {
            case kTableau:

                // Handles empty tableau cards (children are kings of all suits)
                if (rank.empty()) {
                    child_rank  = "K";
                    child_suits = SUITS;
                }
                // Handles regular cards (except aces)
                else if (rank != "A") {
                    child_rank  = RANKS.at(GetIndex(RANKS, rank) - 1);
                    child_suits = GetOppositeSuits(suit);
                }
                break;

            case kFoundation:

                // Handles empty foundation cards (children are aces of same suit)
                if (rank.empty()) {
                    child_rank  = "A";
                    child_suits = {suit};
                }
                // Handles regular cards (except kings)
                else if (rank != "K") {
                    child_rank  = RANKS.at(GetIndex(RANKS, rank) + 1);
                    child_suits = {suit};
                }
                break;

            default:
                return legal_children;
        }

        // TODO: Child suits could technically be empty if OppositeSuits() returns {}
        for (const auto & child_suit : child_suits) {
            auto child   = Card(child_rank, child_suit);
            child.hidden = false;
            legal_children.push_back(child);
        }

        return legal_children;
    }

    std::string Card::ToString() const {
        /* // Old Implementation
        if (hidden) {
            return "[] ";
        } else {
            std::string result;
            if (suit == "s" or suit == "c") {
                absl::StrAppend(&result, WHITE, rank, suit, RESET, " ");
            } else if (suit == "h" or suit == "d") {
                absl::StrAppend(&result, RED, rank, suit, RESET, " ");
            }
            return result;
        }
        */

        log("Entering Card::ToString()");

        std::string result;

        if (hidden) {
            // Representation of a hidden card
            absl::StrAppend(&result, "\U0001F0A0", " ");
        }
        else {
            // Suit Color
            if (suit == "s" or suit == "c") {
                absl::StrAppend(&result, WHITE);
            } else if (suit == "h" or suit == "d") {
                absl::StrAppend(&result, RED);
            }

            // Special Cards
            if (rank.empty()) {
                // Handles special tableau cards which have no rank or suit
                if (suit.empty()) {
                    absl::StrAppend(&result, "\U0001F0BF");
                }
                // Handles special foundation cards which have a suit but not a rank
                else {
                    if (suit == "s") {
                        absl::StrAppend(&result, "\U00002660");
                    } else if (suit == "h") {
                        absl::StrAppend(&result, "\U00002665");
                    } else if (suit == "c") {
                        absl::StrAppend(&result, "\U00002663");
                    } else if (suit == "d") {
                        absl::StrAppend(&result, "\U00002666");
                    }
                }
            }

            // Ordinary Cards
            else {
                absl::StrAppend(&result, rank, suit);
            }



        }

        absl::StrAppend(&result, RESET, " ");
        return result;
    }

    // Deck Methods ====================================================================================================

    Deck::Deck() {
        log("Entering Deck()");

        for (int i = 1; i <= 24; i++) {
            cards.emplace_back();
        }
        for (auto & card : cards) {
            card.location = kDeck;
        }
    }

    std::vector<Card> Deck::Sources() const {
        log("Entering Deck::Sources()");

        // TODO: Can simplify this if statement

        // If the waste is not empty, sources is just a vector of the top card of the waste
        if (not waste.empty()) {
            if (waste.front().hidden) {
                return {};
            } else {
                return {waste.front()};
            }
        }
        // If it is empty, sources is just an empty vector
        else {
            return {};
        }

    }

    std::vector<Card> Deck::Split(Card card) {
        log("Entering Deck::Split()");

        std::vector<Card> split_cards;
        if (waste.front() == card) {
            split_cards = {waste.front()};
            waste.pop_front();
            return split_cards;
        }
    }

    void Deck::draw(unsigned long num_cards) {
        log("Entering Deck::draw()");

        std::deque<Card> drawn_cards;
        num_cards = std::min(num_cards, cards.size());

        int i = 1;
        while (i <= num_cards) {
            auto card = cards.front();
            card.location = kWaste;
            drawn_cards.push_back(card);
            cards.pop_front();
            i++;
        }

        waste.insert(waste.begin(), drawn_cards.begin(), drawn_cards.end());
    }

    void Deck::rebuild() {
        log("Entering Deck::rebuild()");

        // TODO: Make sure cards and initial_order are never both empty at the same time.
        if (cards.empty()) {
            for (Card & card : initial_order) {
                if (std::find(waste.begin(), waste.end(), card) != waste.end()) {
                    card.location = kDeck;
                    cards.push_back(card);
                }
            }
            waste.clear();
            times_rebuilt += 1;
        } else {
            std::cout << YELLOW << "WARNING: Cannot rebuild a non-empty deck" << RESET << std::endl;
        }
    }

    // Foundation Methods ==============================================================================================

    Foundation::Foundation() {
        log("Entering Foundation()");

        cards = {};
    }

    Foundation::Foundation(std::string suit) : suit(std::move(suit)) {
        log("Entering Foundation(std::string suit)");

        cards = {};
    }

    std::vector<Card> Foundation::Sources() const {
        log("Entering Foundation::Sources()");

        // If the foundation is not empty, sources is just a vector of the top card of the foundation
        if (not cards.empty()) {
            return {cards.back()};
        }
        // If it is empty, then sources is just an empty vector
        else {
            return {};
        }
    }

    std::vector<Card> Foundation::Targets() const {
        log("Entering Foundation::Targets()");

        // If the foundation is not empty, targets is just the top card of the foundation
        if (not cards.empty()) {
            return {cards.back()};
        }
        // If it is empty, then targets is just a special card with no rank and a suit matching this foundation
        else {
            auto card     = Card("", suit);
            card.hidden   = false;
            card.location = kFoundation;
            return {card};
        }
    }

    std::vector<Card> Foundation::Split(Card card) {
        log("Entering Foundation::Split()");

        std::vector<Card> split_cards;
        if (cards.back() == card) {
            split_cards = {cards.back()};
            cards.pop_back();
            return split_cards;
        }
    }

    void Foundation::Extend(const std::vector<Card> & source_cards) {
        log("Entering Foundation::Extend()");

        // TODO: Can only extend with ordinary cards
        // TODO: Probably no use for setting hidden to false.

        for (auto card : source_cards) {
            card.location = kFoundation;
            cards.push_back(card);
        }
    }

    // Tableau Methods =================================================================================================

    Tableau::Tableau() {
        log("Entering Tableau()");
    }

    Tableau::Tableau(int num_cards) {
        log("Entering Tableau(int num_cards)");

        for (int i = 1; i <= num_cards; i++) {
            cards.emplace_back();
        }
        for (auto & card : cards) {
            card.location = kTableau;
        }
    }

    std::vector<Card> Tableau::Sources() const {
        log("Entering Tableau::Sources()");

        // If the tableau is not empty, sources is just a vector of all cards that are not hidden
        if (not cards.empty()) {
            std::vector<Card> sources;
            for (auto & card : cards) {
                if (not card.hidden) {
                    sources.push_back(card);
                }
            }
            return sources;
        }
        // If it is empty, then sources is just an empty vector
        else {
            return {};
        }
    }

    std::vector<Card> Tableau::Targets() const {
        /*
        DECISION: Should targets return a vector, even though it will only return one card?
        */
        log("Entering Tableau::Targets()");

        // If the tableau is not empty, targets is just a vector of the top card of the tableau
        if (not cards.empty()) {
            if (cards.back().hidden) {
                return {};
            } else {
                return {cards.back()};
            }
        }
        // If it is empty, then targets is just a special card with no rank or suit
        else {
            auto card     = Card();
            card.hidden   = false;
            card.location = kTableau;
            return {card};
        }
    }

    std::vector<Card> Tableau::Split(Card card) {
        log("Entering Tableau::Split()");

        // TODO: How to handle a split when card isn't in this tableau?

        std::vector<Card> split_cards;
        if (not cards.empty()) {
            bool split_flag = false;
            for (auto it = cards.begin(); it != cards.end();) {
                if (*it == card) {
                    split_flag = true;
                }
                if (split_flag) {
                    split_cards.push_back(*it);
                    it = cards.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
            std::cout << YELLOW << "WARNING: Cannot split an empty tableau" << RESET << std::endl;
        }

        return split_cards;
    }

    void Tableau::Extend(const std::vector<Card> & source_cards) {
        log("Entering Tableau::Extend()");

        for (auto card : source_cards) {
            card.location = kTableau;
            cards.push_back(card);
        }
    }

    // Move Methods ====================================================================================================

    Move::Move(Card target_card, Card source_card) {
        log("Entering Move(target_card, source_card)");

        target = std::move(target_card);
        source = std::move(source_card);
    }

    Move::Move(Action action_id) {
        log("Entering Move(action_id)");

        auto card_pair = ACTION_TO_MOVE.at(action_id);
        target = Card(card_pair.first);
        source = Card(card_pair.second);
    }

    std::string Move::ToString() const {
        log("Entering Move::ToString()");

        std::string result;
        absl::StrAppend(&result, target.ToString(), "\U00002190", " ",source.ToString());
        return result;
    }

    Action Move::ActionId() const {
        log("Entering Move::ActionId()");

        return MOVE_TO_ACTION.at(std::make_pair((int) target, (int) source));
    }

    // SolitaireState Methods ==========================================================================================

    SolitaireState::SolitaireState(std::shared_ptr<const Game> game) :
        State(game),
        deck(),
        foundations(),
        tableaus() {
            log("Entering SolitaireState(game)");
            is_started = false;
            is_setup = false;
            previous_score = 0.0;
        }

    // Overriden Methods -----------------------------------------------------------------------------------------------

    Player                  SolitaireState::CurrentPlayer() const {
        log("Entering CurrentPlayer()");

        // There are only two players in this game: chance and player 1.
        if (IsChanceNode()) {
            // Index of the chance player
            return kChancePlayerId;
        } else {
            // Index of the player
            return 0;
        }
    }

    std::unique_ptr<State>  SolitaireState::Clone() const {
        log("Entering Clone()");

        return std::unique_ptr<State>(new SolitaireState(*this));
    }

    bool                    SolitaireState::IsTerminal() const {
        log("Entering IsTerminal()");


        if (is_finished or draw_counter >= 8) {
            return true;
        }

        else if (History().size() >= 8) {

            std::vector<Action> history = History();
            std::vector<Action> recent_history(history.end() - 8, history.end());

            for (auto action : recent_history) {
                if (action != kDraw) {
                    return false;
                }
            }

            // If all 8 recent actions are kDraw, then the state is terminal
            return true;

        }

        else {
            return false;
        }

    }

    bool                    SolitaireState::IsChanceNode() const {
        log("Entering IsChanceNode()");

        if (not is_setup) {
            // If setup is not started, this is a chance node
            return true;
        }

        else {

            // If there is a hidden card on the top of a tableau, this is a chance ndoe
            for (auto & tableau : tableaus) {
                if (tableau.cards.empty()) {
                    continue;
                }
                else if (tableau.cards.back().hidden) {
                    return true;
                }
            }

            // If any card in the waste is hidden, this is a chance node
            if (not deck.waste.empty()) {
                for (auto & card : deck.waste) {
                    if (card.hidden) {
                        return true;
                    }
                }
            }

            // Otherwise, this is node a chance node; it's a decision node
            return false;

        }
    }

    std::string             SolitaireState::ToString() const {
        log("Entering ToString()");

        std::string result;

        //absl::StrAppend(&result, "\nIS_SETUP       : ", is_setup);
        //absl::StrAppend(&result, "\nIS_STARTED     : ", is_started);

        absl::StrAppend(&result, "\nCURRENT PLAYER : ", CurrentPlayer());
        absl::StrAppend(&result, "\nDRAW COUNTER   : ", draw_counter);

        absl::StrAppend(&result, "\n\nDECK        : ");
        for (const Card & card : deck.cards) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\nWASTE       : ");
        for (const Card & card : deck.waste) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\nORDER       : ");
        for (const Card & card : deck.initial_order) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\nFOUNDATIONS : ");
        for (const Foundation & foundation : foundations) {
            if (foundation.cards.empty()) {
                Card foundation_base = Card("", foundation.suit);
                foundation_base.hidden = false;
                absl::StrAppend(&result, foundation_base.ToString());
            } else {
                absl::StrAppend(&result, foundation.cards.back().ToString());
            }
        }

        absl::StrAppend(&result, "\nTABLEAUS    : ");
        for (const Tableau & tableau : tableaus) {
            if (not tableau.cards.empty()) {
                absl::StrAppend(&result, "\n");
                for (const Card & card : tableau.cards) {
                    absl::StrAppend(&result, card.ToString());
                }
            }
        }

        /*
        absl::StrAppend(&result, "\n\nTARGETS : ");
        for (const Card & card : Targets()) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\nSOURCES : ");
        for (const Card & card : Sources()) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\n\nCANDIDATE MOVES : ");
        for (const Move & move : CandidateMoves()) {
            absl::StrAppend(&result, "\n", move.ToString(), ": ", move.ActionId());
            absl::StrAppend(&result, ", ", IsReversible(move));
        }
        */

        return result;
    }

    std::string             SolitaireState::ActionToString(Player player, Action action_id) const {
        // TODO: Probably use the enum names instead of the values the represent

        log("Entering ActionToString()");

        switch (action_id) {
            case kSetup : {
                return "kSetup";
            }
            case kRevealAs ... kRevealKd : {
                // Reveal starts at 1 while card indices start at 0, so we subtract one here
                Card revealed_card = Card(action_id - 1);

                std::string result;
                absl::StrAppend(&result, "kReveal", revealed_card.rank, revealed_card.suit);

                return result;
            }
            case kDraw : {
                return "kDraw";
            }
            case kMove__Ks ... kMoveKdQc : {
                Move move = Move(action_id);
                std::string result;

                absl::StrAppend(&result, "kMove");
                if (move.target.rank.empty()) { absl::StrAppend(&result, "__"); }
                else { absl::StrAppend(&result, move.target.rank, move.target.suit); }
                absl::StrAppend(&result, move.source.rank, move.source.suit);

                return result;
            }
            default : {
                return "kMissingAction";
            }
        }
    }

    std::string             SolitaireState::InformationStateString(Player player) const {
        log("Entering InformationStateString()");

        return HistoryString();
    }

    std::string             SolitaireState::ObservationString(Player player) const {
        log("Entering ObservationString()");

        std::string result;

        absl::StrAppend(&result, "\n\nDECK        : ");
        for (const Card & card : deck.cards) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\nWASTE       : ");
        for (const Card & card : deck.waste) {
            absl::StrAppend(&result, card.ToString());
        }

        absl::StrAppend(&result, "\nFOUNDATIONS : ");
        for (const Foundation & foundation : foundations) {
            if (foundation.cards.empty()) {
                Card foundation_base = Card("", foundation.suit);
                foundation_base.hidden = false;
                absl::StrAppend(&result, foundation_base.ToString());
            } else {
                absl::StrAppend(&result, foundation.cards.back().ToString());
            }
        }

        absl::StrAppend(&result, "\nTABLEAUS    : ");
        for (const Tableau & tableau : tableaus) {
            if (not tableau.cards.empty()) {
                absl::StrAppend(&result, "\n");
                for (const Card & card : tableau.cards) {
                    absl::StrAppend(&result, card.ToString());
                }
            }
        }

        return result;

    }

    void                    SolitaireState::InformationStateTensor(Player player, std::vector<double> *values) const {
        log("Entering InformationStateTensor()");

        values->resize(game_->InformationStateTensorShape()[0]);
        std::fill(values->begin(), values->end(), kInvalidAction);

        int i = 0;
        for (auto & action : History()) {
            (*values)[i] = action;
            ++i;
        }

    }

    void                    SolitaireState::ObservationTensor(Player player, std::vector<double> *values) const {

        log("Entering ObservationTensor()");

        // TODO: Not sure if any pile being empty would have an effect on the final result

        for (const auto & tableau : tableaus) {
            std::vector<double> tableau_obs = ToCardIndices(tableau.cards, 19);
            values->insert(values->end(), tableau_obs.begin(), tableau_obs.end());
        }

        for (const auto & foundation : foundations) {
            std::vector<double> foundation_obs = ToCardIndices(foundation.cards, 13);
            values->insert(values->end(), foundation_obs.begin(), foundation_obs.end());
        }

        std::vector<double> waste_obs = ToCardIndices(deck.waste, 24);
        values->insert(values->end(), waste_obs.begin(), waste_obs.end());

        std::vector<double> deck_obs = ToCardIndices(deck.cards, 24);
        values->insert(values->end(), deck_obs.begin(), deck_obs.end());

    }

    void                    SolitaireState::DoApplyAction(Action move) {
        log("Entering DoApplyAction()");

        // Set previous_score to be equal to the returns from this state

        previous_score = Returns().front();

        // Action Handling =============================================================================================

        // Handles kSetup
        if (move == kSetup) {
            log("DoApplyAction() - if (move == kSetup)");

            // Creates tableaus
            for (int i = 1; i <= 7; i++) {
                tableaus.emplace_back(i);
            }

            // Creates foundations
            for (const auto & suit : SUITS) {
                foundations.emplace_back(suit);
            }

            is_setup       = true;
            is_started     = false;
            is_finished     = false;
            is_reversible  = false;
            draw_counter   = 0;
            previous_score = 0.0;

        }

        // Handles kReveal
        else if (1 <= move and move <= 52) {
            log("DoApplyAction() - else if (1 <= move and move <= 52)");

            // Cards start at 0 instead of 1 which is why we subtract 1 to move here.
            Card revealed_card = Card(move - 1);
            bool found_hidden_card = false;

            // For tableau in tableaus ...
            for (auto & tableau : tableaus) {

                // If it isn't empty ...
                if (not tableau.cards.empty()) {

                    // If the last card is hidden ...
                    if (tableau.cards.back().hidden) {

                        // Then reveal it
                        tableau.cards.back().rank = revealed_card.rank;
                        tableau.cards.back().suit = revealed_card.suit;
                        tableau.cards.back().hidden = false;

                        // And indicate that we found a hidden card so we don't have to search for one in the waste
                        found_hidden_card = true;

                        // Breaks out and goes to check the waste, if applicable
                        break;
                    }
                }
            }

            // If we didn't find a hidden card in the tableau and the waste isn't empty ...
            if ((not found_hidden_card) and not deck.waste.empty()) {

                // Then for card in the waste ...
                for (auto & card : deck.waste) {

                    // If the card is hidden ...
                    if (card.hidden) {

                        // Reveal it by setting its rank and suit
                        card.rank = revealed_card.rank;
                        card.suit = revealed_card.suit;
                        card.hidden = false;

                        // Add the revealed card to the initial order
                        deck.initial_order.push_back(card);
                        break;

                    }
                }
            }

            // Add move to revealed cards so we don't try to reveal it again
            revealed_cards.push_back(move);


            // TODO: There shouldn't ever be a time before `is_started` where a tableau is empty
            // If the game hasn't been started ...
            if (not is_started) {
                // For every tableau in tableaus ...
                for (auto & tableau : tableaus) {
                    // If the last card is hidden ...
                    if (tableau.cards.back().hidden) {
                        // Then we are not ready to start the game.
                        // Return with is_started still false;
                        return;
                    }
                    // If the last card is not hidden, continue the loop and check the next tableau
                    else {
                        continue;
                    }
                }

                // This is only reached if all cards at the back of the tableaus are not hidden.
                is_started = true;
                previous_score = 0.0;
            }

        }

        // Handles kDraw
        else if (move == kDraw) {
            log("DoApplyAction() - else if (move == kDraw)");
            // kDraw is not reversible (well, you'd have to go through the deck again)
            // is_reversible = false;
            if (deck.cards.empty()) {
                deck.rebuild();
            }
            deck.draw(3);

            // Loop Detection
            std::vector<Action> legal_actions = LegalActions();

            // We check here if there are any other legal actions besides kDraw
            if (legal_actions.size() == 1) {
                draw_counter += 1;
            }

            if (draw_counter >= 8) {
                is_finished = true;
            }
        }

        // Handles kMove
        else {
            log("DoApplyAction() - else");
            // Create a move from the action id provided by 'move'

            Move selected_move = Move(move);

            // If the move we are about to execute is reversible, set to true, else set to false
            is_reversible = IsReversible(selected_move);

            // Execute the selected move
            MoveCards(selected_move);

            // Reset the draw_counter if it's not below 8
            if (draw_counter <= 8) {
                draw_counter = 0;
            }

        }

        // Finish Game =================================================================================================

        if (IsSolvable()) {
            // Clear Tableaus
            for (auto & tableau : tableaus) {
                tableau.cards.clear();
            }

            // Clear Foundations & Repopulate
            for (auto & foundation : foundations) {
                foundation.cards.clear();
                for (const auto & rank : RANKS) {
                    Card card = Card(rank, foundation.suit);
                    card.hidden = false;
                    card.location = kFoundation;
                    foundation.cards.push_back(card);
                }
            }

            // Set Game to Finished
            is_finished = true;
        }



    }

    std::vector<double>     SolitaireState::Returns() const {
        // Equal to the sum of all rewards up to the current state

        log("Entering Returns()");

        if (is_started) {

            double returns = 0.0;

            // Foundation Score
            double foundation_score = 0.0;
            for (auto & foundation : foundations) {
                for (auto & card : foundation.cards) {
                    foundation_score += FOUNDATION_POINTS.at(card.rank);
                }
            }

            // Tableau Score
            double tableau_score = 0.0;
            int num_hidden_cards = 0;
            for (auto & tableau : tableaus) {
                if (not tableau.cards.empty()) {
                    for (auto & card : tableau.cards) {
                        // Cards that will be revealed by a chance node next turn are not counted
                        if (card.hidden) {
                            num_hidden_cards += 1;
                        }
                    }
                    if (tableau.cards.back().hidden) {
                        num_hidden_cards += -1;
                    }
                }
            }
            tableau_score = (21 - num_hidden_cards) * 20;

            // Waste Score
            double waste_score = 0.0;
            int waste_cards_remaning;
            waste_cards_remaning = deck.cards.size() + deck.waste.size();
            waste_score = (24 - waste_cards_remaning) * 20;

            // Total Score
            returns = foundation_score + tableau_score + waste_score;
            return {returns};
        }

        else {
            return {0.0};
        }

    }

    std::vector<double>     SolitaireState::Rewards() const {
        // TODO: Should not be called on chance nodes (undefined and crashes)
        // Highest possible reward per action is 120.0 (e.g. ♠ ← As where As is on a hidden card)
        // Lowest possible reward per action is -100.0 (e.g. 2h ← As where As is in foundation initially) */
        log("Entering Rewards()");

        if (is_started) {
            std::vector<double> current_returns = Returns();
            double current_score = current_returns.front();
            return {current_score - previous_score};
        } else {
            return {0.0};
        }

    }

    std::vector<Action>     SolitaireState::LegalActions() const {
        log("Entering LegalActions()");

        if (IsTerminal()) {
            return {};
        }

        else {
            // TODO: What is CandidateMoves() is empty?

            std::vector<Action> legal_actions;

            for (const auto & move : CandidateMoves()) {

                if (is_reversible) {
                    if (IsReversible(move)) {
                        continue;
                    } else {
                        legal_actions.push_back(move.ActionId());
                    }
                } else {
                    legal_actions.push_back(move.ActionId());
                }
            }

            if (deck.cards.size() + deck.waste.size() > 0 and draw_counter < 8) {
                legal_actions.push_back(kDraw);
            }

            return legal_actions;
        }

    }

    std::vector<std::pair<Action, double>> SolitaireState::ChanceOutcomes() const {
        log("Entering ChanceOutcomes");

        if (!is_setup) {
            return {{kSetup, 1.0}};
        } else {
            std::vector<std::pair<Action, double>> outcomes;
            const double p = 1.0 / (52 - revealed_cards.size());

            for (int i = 1; i <= 52; i++) {
                if (std::find(revealed_cards.begin(), revealed_cards.end(), i) != revealed_cards.end()) {
                    continue;
                } else {
                    outcomes.emplace_back(i, p);
                }
            }
            return outcomes;
        }
    }

    // Other Methods ---------------------------------------------------------------------------------------------------

    std::vector<Card>       SolitaireState::Targets(const std::optional<std::string> & location) const {
        log("Entering SolitaireState::Targets()");

        std::string loc = location.value_or("all");
        std::vector<Card> targets;

        // Gets targets from tableaus
        if (loc == "tableau" or loc == "all") {
            for (const Tableau & tableau : tableaus) {
                std::vector<Card> current_targets = tableau.Targets();
                targets.insert(targets.end(), current_targets.begin(), current_targets.end());
            }
        }

        // Gets targets from foundations
        if (loc == "foundation" or loc == "all") {
            for (const Foundation & foundation : foundations) {
                std::vector<Card> current_targets = foundation.Targets();
                targets.insert(targets.end(), current_targets.begin(), current_targets.end());
            }
        }

        // Returns targets as a vector of cards in all piles specified by "location"
        return targets;

    }

    std::vector<Card>       SolitaireState::Sources(const std::optional<std::string> & location) const {
        log("Entering SolitaireState::Sources()");

        std::string loc = location.value_or("all");
        std::vector<Card> sources;

        // Gets sources from tableaus
        if (loc == "tableau" or loc == "all") {
            for (const Tableau & tableau : tableaus) {
                std::vector<Card> current_sources = tableau.Sources();
                sources.insert(sources.end(), current_sources.begin(), current_sources.end());
            }
        }

        // Gets sources from foundations
        if (loc == "foundation" or loc == "all") {
            for (const Foundation & foundation : foundations) {
                std::vector<Card> current_sources = foundation.Sources();
                sources.insert(sources.end(), current_sources.begin(), current_sources.end());
            }
        }

        // Gets sources from waste
        if (loc == "waste" or loc == "all") {
            std::vector<Card> current_sources = deck.Sources();
            sources.insert(sources.end(), current_sources.begin(), current_sources.end());
        }

        // Returns sources as a vector of cards in all piles specified by "location"
        return sources;
    }

    std::vector<Move>       SolitaireState::CandidateMoves() const {
        log("Entering CandidateMoves()");

        std::vector<Move> candidate_moves;
        std::vector<Card> targets = Targets();
        std::vector<Card> sources = Sources();

        for (const auto & target : targets) {
            std::vector<Card> legal_children = target.LegalChildren();

            for (auto source : legal_children) {

                source.location = FindLocation(source); // OPTIMIZE

                if (std::find(sources.begin(), sources.end(), source) != sources.end()) {

                    if (target.location == kFoundation and source.location == kTableau) {
                        if (IsTopCard(source)) {
                            candidate_moves.emplace_back(target, source);
                        }
                    }

                    else if (target == Card("", "") and source.rank == "K") {
                        if (not IsBottomCard(source)) {
                            candidate_moves.emplace_back(target, source);
                        }
                    }

                    else {
                        candidate_moves.emplace_back(target, source);
                    }

                }

                else {
                    continue;
                }

            }
        }

        return candidate_moves;

    }

    Tableau *               SolitaireState::FindTableau(const Card & card) const {
        log("Entering FindTableau()");

        if (card.rank.empty() and card.suit.empty()) {
            for (auto & tableau : tableaus) {
                if (tableau.cards.empty()) {
                    return const_cast<Tableau *>(& tableau);
                }
            }
        }

        else {
            for (auto & tableau : tableaus) {
                if (not tableau.cards.empty()) {
                    if (std::find(tableau.cards.begin(), tableau.cards.end(), card) != tableau.cards.end()) {
                        return const_cast<Tableau *>(& tableau);
                    }
                }
            }
        }

    }

    Foundation *            SolitaireState::FindFoundation(const Card & card) const {
        log("Entering FindFoundation");

        if (card.rank.empty()) {
            for (auto & foundation : foundations) {
                if (foundation.cards.empty() and foundation.suit == card.suit) {
                    return const_cast<Foundation *>(& foundation);
                }
            }
        }
        else {
            for (auto & foundation : foundations) {
                if (not foundation.cards.empty() and foundation.suit == card.suit) {
                    if (std::find(foundation.cards.begin(), foundation.cards.end(), card) != foundation.cards.end()) {
                        return const_cast<Foundation *>(& foundation);
                    }
                }
            }
        }

    }

    Location                SolitaireState::FindLocation(const Card & card) const {
        // OPTIMIZE

        log("Entering FindLocation()");

        // Handles special cards
        if (card.rank.empty()) {
            if (card.suit.empty()) {
                return kTableau;
            } else {
                return kFoundation;
            }
        }

        // Attempts to find the card in a tableau
        for (auto & tableau : tableaus) {
            if (std::find(tableau.cards.begin(), tableau.cards.end(), card) != tableau.cards.end()) {
                return kTableau;
            }
        }

        // Attempts to find the card in a foundation
        for (auto & foundation : foundations) {
            if (std::find(foundation.cards.begin(), foundation.cards.end(), card) != foundation.cards.end()) {
                return kFoundation;
            }
        }

        // Attempts to find the card in the waste
        if (std::find(deck.waste.begin(), deck.waste.end(), card) != deck.waste.end()) {
            return kWaste;
        }

        // Attempts to find the card in the deck
        if (std::find(deck.cards.begin(), deck.cards.end(), card) != deck.cards.end()) {
            return kDeck;
        }

        // Default value is returned if the card isn't found
        return kMissing;

    }

    void                    SolitaireState::MoveCards(const Move & move) {
        log("Entering MoveCards()");

        // Unpack target and source from move
        Card target = move.target;
        Card source = move.source;

        // Find their locations in this state
        target.location = FindLocation(target);
        source.location = FindLocation(source);

        std::vector<Card> split_cards;

        switch (source.location) {
            case kTableau : {
                split_cards = FindTableau(source)->Split(source);
                break;
            }
            case kFoundation : {
                split_cards = FindFoundation(source)->Split(source);
                break;
            }
            case kWaste : {
                split_cards = deck.Split(source);
                break;
            }
            default : {
                std::cout << YELLOW << "WARNING: 'source' is not in a tableau, foundation, or waste" << RESET << std::endl;
                std::cout << YELLOW << "WARNING: 'source' = " << source.ToString() << RESET << std::endl;
            }
        }

        switch (target.location) {
            case kTableau : {
                auto target_container = FindTableau(target);
                target_container->Extend(split_cards);
                break;
            }
            case kFoundation : {
                auto target_container = FindFoundation(target);
                target_container->Extend(split_cards);
                break;
            }
            default : {
                std::cout << YELLOW << "WARNING: 'target' is not in a tableau or foundation" << RESET << std::endl;
                std::cout << YELLOW << "WARNING: 'target' = " << target.ToString() << RESET << std::endl;
            }
        }

    }

    bool                    SolitaireState::IsOverHidden(const Card & card) const {
        log("Entering IsOverHidden()");

        if (card.location == kTableau) {
            auto container = FindTableau(card);
            auto p = std::find(container->cards.begin(), container->cards.end(), card);
            auto previous_card = std::prev(p);
            return previous_card->hidden;
        }

        return false;
    }

    bool                    SolitaireState::IsReversible(const Move & move) const {
        log("Entering IsReversible()");

        Card target = move.target;
        Card source = move.source;

        target.location = FindLocation(target);
        source.location = FindLocation(source);

        switch (source.location) {
            // Cards cannot be moved back to the waste, therefore this is not reversible
            case kWaste : {
                return false;
            }
            // Cards can always be moved back from the foundation on the next state
            case kFoundation : {
                return true;
            }
            // Cards can be moved back if they don't reveal a hidden card upon being moved
            case kTableau : {
                if (IsBottomCard(source) or IsOverHidden(source)) {
                    return false;
                } else {
                    return true;
                }
            }
            // Cards can't be moved at all if they are in kDeck or kMissing
            default : {
                std::cout << YELLOW << "WARNING: 'source' is not in a tableau, foundation, or waste" << RESET << std::endl;
                // I guess we return false here since it's not even a valid move?
                return false;
            }

        }
    }

    bool                    SolitaireState::IsBottomCard(Card card) const {
        log("Entering IsBottomCard()");

        // Only implemented for cards in a tableau at the moment.
        if (card.location == kTableau) {
            auto container = FindTableau(card);
            // This line assumes three things:
            //  - That `FindTableau()` actually found a tableau (TODO: add exception to it later)
            //  - That the tableau found is not empty, otherwise we could not call `cards.front()`
            //  - That `card` is an ordinary card (e.g. its suit and rank are defined)
            return container->cards.front() == card;
        } else {
            // While it a card could be the bottom one in different locations, there isn't much use
            return false;
        }
    }

    bool                    SolitaireState::IsTopCard(const Card & card) const {
        // Assumes that card is found in a container, meaning container.back() will be defined
        // This isn't true for special cards (e.g. Card("", "") or Card("", "s") etc.)

        log("Entering IsTopCard()");

        std::deque<Card> container;
        switch (card.location) {
            case kTableau : {
                container = FindTableau(card)->cards;
                return card == container.back();
            }
            case kFoundation : {
                container = FindFoundation(card)->cards;
                return card == container.back();
            }
            case kWaste : {
                container = deck.waste;
                return card == container.front();
            }
            default : {
                return false;
            }
        }
    }

    bool                    SolitaireState::IsSolvable() const {
        log("Entering IsSolvable()");

        if (deck.cards.empty() and deck.waste.empty()) {
            for (auto & tableau : tableaus) {
                if (not tableau.cards.empty()) {
                    for (auto & card : tableau.cards) {
                        // Returns false if at least one tableau card is hidden
                        if (card.hidden) {
                            return false;
                        }
                    }
                } else {
                    continue;
                }
            }
            // Only returns true if all cards are revealed and there are no cards in deck or waste
            return true;
        }
        else {
            // Returned if there are cards in deck or waste
            return false;
        }

    }

    // SolitaireGame Methods ===========================================================================================

    SolitaireGame::SolitaireGame(const GameParameters & params) :
        Game(kGameType, params),
        num_players_(ParameterValue<int>("players")) {

    }

    int     SolitaireGame::NumDistinctActions() const {
        return 206;
    }

    int     SolitaireGame::MaxGameLength() const {
        return 300;
    }

    int     SolitaireGame::NumPlayers() const {
        return 1;
    }

    double  SolitaireGame::MinUtility() const {
        return 0.0;
    }

    double  SolitaireGame::MaxUtility() const {
        return 3220.0;
    }

    std::vector<int> SolitaireGame::InformationStateTensorShape() const {
        return {1000};
    }

    std::vector<int> SolitaireGame::ObservationTensorShape() const {
        return {233};
    }

    std::unique_ptr<State> SolitaireGame::NewInitialState() const {
        return std::unique_ptr<State>(new SolitaireState(shared_from_this()));
    }

    std::shared_ptr<const Game> SolitaireGame::Clone() const {
        return std::shared_ptr<const Game>(new SolitaireGame(*this));
    }


} // namespace open_spiel::solitaire


