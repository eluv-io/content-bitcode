#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/cddl.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
#include "eluvio/media.h"

using namespace elv_context;



class ChallengeEngine {
public:
    typedef std::pair<int32_t,int32_t> bracket_type;

    static inline uint32_t ilog2(const uint32_t x) {
        return (31 - __builtin_clz (x));
    }
    static unsigned int ilog16 (unsigned int val) {
        if (val == 0) return UINT_MAX;
        if (val == 1) return 0;
        unsigned int ret = 0;
        while (val > 1) {
            val >>= 4;
            ret++;
        }
        return ret;
    }
    inline int32_t value_at_pos(int32_t pos, int32_t digits_per){
        return strtol(m_state.substr(pos, digits_per).c_str(), NULL, 16);
    }
    bracket_type get_parents(BitCodeCallContext* ctx, bracket_type& child){
        int parent_level = child.first;
        switch(parent_level){
            case 3:
                return bracket_type(value_at_pos(12, 1), value_at_pos(13, 1));
            case 2:
                return bracket_type(value_at_pos(8+(child.second*2), 1), value_at_pos(8+(child.second*2)+1, 1));
            case 1:
                return bracket_type(value_at_pos(2*(child.second), 1), value_at_pos(2*(child.second)+1, 1));
            case 0:
                int len = m_state.length();
                return bracket_type(len*2, len*2 + 1);
        };
        LOG_ERROR(ctx, "Unhandled default in switch get_parents", "state", m_state);
        return bracket_type(0,0);

    }
    bracket_type find_bracket(int32_t len){
        int brackets = ilog2(m_base);
        int32_t flight = m_base;

        for (int i = 0; i < brackets; i++){
            flight = flight/2;

            if (len < flight){
                return bracket_type(i, len);
            }
            len -= flight;
        }
        return bracket_type(brackets, 0);
    }
    void adjust_state(int vote,  int digits){
        switch(vote){
            case -1:
                break;
            case 0:
            case 1:
                int i = 0;
                int len = m_state.length();
                int row = m_base/2;
                int pos = len-row;
                if (pos < 0){
                    int val = len*2 + vote;
                    char str_val[1024];
                    sprintf(str_val, "%01x", val); //REVIEW THIS NEEDS CHAR SIZE CHANGE FOR > 16
                    m_state += str_val;
                    break;
                }
                while (pos > row/2){
                    row /= 2;
                    pos -= row;
                    i++;
                }
                row = m_base/2;
                pos *= 2;
                while (i >= 1){
                    pos += row;
                    row /= 2;
                    i--;
                }
                m_state += m_state[pos+vote];
        };
    }

    std::string make_url(std::string hash, bool is_dash = true){
        std::ostringstream oss;
        if (is_dash){
            oss << "https://main.net955305.contentfabric.io/q/" << hash << "/rep/playout/default/dash-clear/dash.mpd";
        }else{
            oss << "https://main.net955305.contentfabric.io/q/" << hash << "/rep/playout/default/hls-clear/playlist.m3u8";
        }

        return oss.str();
    }

    void insert_video(std::stringstream& oss, string label, string id, string hash){
        oss << "{ \"label\": \"" << label << "\", ";
        oss << " \"id\": \"" << id << "\", ";
        oss << " \"hls\": \"" << make_url(hash,false) << "\", ";
        oss << " \"dash\": \"" << make_url(hash) << "\"";
        oss << "}";
    }

    elv_return_type urls(BitCodeCallContext* ctx, int video0, int video1, nlohmann::json& entries_a, nlohmann::json& entries_b,nlohmann::json& rounds){
        try{
            auto asset_meta0 = entries_a[video0]["/"].get<std::string>();
            auto asset_meta1 = entries_b[video1]["/"].get<std::string>();
            const auto qfab_pre = "qfab/";
            auto qfabLoc0 = asset_meta0.find(qfab_pre);
            auto qfabLoc1 = asset_meta1.find(qfab_pre);
            auto hash0 = asset_meta0.substr(qfabLoc0 + strlen(qfab_pre), asset_meta0.find("/", qfabLoc0 + strlen(qfab_pre))-(qfabLoc0 + strlen(qfab_pre)));
            auto hash1 = asset_meta1.substr(qfabLoc1 + strlen(qfab_pre), asset_meta1.find("/", qfabLoc1 + strlen(qfab_pre))-(qfabLoc1 + strlen(qfab_pre)));

            std::string lookup0 = "/public/asset_metadata/playlists/bracket-snoop-a/list/";
            lookup0 += entries_a[video0]["key"];
            lookup0 += "/display_title";

            std::string lookup1 = "/public/asset_metadata/playlists/bracket-snoop-b/list/";
            lookup1 += entries_b[video1]["key"];
            lookup1 += "/display_title";

            auto pr = ctx->SQMDGetJSONResolve(lookup0.c_str());
            if (pr.second.IsError()){
                return ctx->make_error("unable to find meta video0", pr.second);
            }
            auto video0_label =  pr.first;

            pr = ctx->SQMDGetJSONResolve(lookup1.c_str());
            if (pr.second.IsError()){
                return ctx->make_error("unable to find meta video1",  pr.second);
            }
            auto video1_label =  pr.first;
            std::stringstream  oss;
            oss    << "{\"videos\" : [";
            if (m_is_round_change){
                auto round_meta = rounds[m_round]["/"].get<std::string>();
                auto hash = round_meta.substr(qfabLoc0 + strlen(qfab_pre), round_meta.find("/", qfabLoc0 + strlen(qfab_pre))-(qfabLoc0 + strlen(qfab_pre)));
                std::string round_id = "round";
                std::string round_label = round_id + std::to_string(m_round);
                insert_video(oss,round_label, round_id, hash);
                oss         << ",";
            }
            insert_video(oss, video0_label, std::to_string(0), hash0);
            oss         << ",";
            insert_video(oss, video1_label, std::to_string(1), hash1);
            oss         << "],\"state\":\"" << m_state << "\"}";
            std::string retval = oss.str();
            std::vector<unsigned char> json_bytes;
            copy(istream_iterator<unsigned char>(oss), istream_iterator<unsigned char>(), back_inserter(json_bytes));
            auto p = ctx->Callback(200, "application/json", json_bytes.size());
            if (p.second.IsError()){
                return ctx->make_error("callback failure", p.second);
            }
            p = ctx->WriteOutput(json_bytes);
            if (p.second.IsError()){
                return ctx->make_error("writeoutput failure", p.second);
            }
            nlohmann::json j = nlohmann::json::parse(retval);
            return ctx->make_success(j);
        }
        catch (json::exception& e){
            return ctx->make_error("urls json excpetion occured", E(e.what()));
        }
        catch (std::exception &e) {
            return ctx->make_error("urls std excpetion occured", e.what());
        }
        catch(...){
            return ctx->make_error("urls ... excpetion occured", E("elipsis catch handler make_entrants"));
        }

    }

    int max_length(){
        int sum = 0;
        auto base = m_base;
        while (base >= 1){
            base /= 2;
            sum += base;
        }
        return sum;
    }
    static bool compare_elements(nlohmann::json l, nlohmann::json r){
        return l["order"].get<int32_t>() < r["order"].get<int32_t>();
    }
    // turning a json list into an array
    elv_return_type make_list(BitCodeCallContext* ctx, nlohmann::json& entriesList, const char* list){
        try{
            auto pub = entriesList["public"];
            auto meta = pub["asset_metadata"];
            auto playlists = meta["playlists"];
            auto playlist = playlists[list]["list"];
            auto playlist_count = playlists[list]["count"];

            nlohmann::json return_array = json::array();
            for (auto& it : playlist.items()){
                auto el = it.value();
                if (el.is_null())
                    continue;
                el["key"] = it.key();
                cout << "KEY:" << it.key() << " VALUE:" << el << endl;
                return_array.push_back(el);
            }
            std::sort(return_array.begin(), return_array.end(), compare_elements);
            cout << "array" << return_array << endl;
            cout << "size" << return_array.size() << endl;

            return ctx->make_success(return_array);
        }
        catch (json::exception& e){
            return ctx->make_error("make_list excpetion occured", E(e.what()));
        }
        catch (std::exception &e) {
            return ctx->make_error("make_list std excpetion occured", e.what());
        }
        catch(...){
            return ctx->make_error("make_list ... excpetion occured", E("elipsis catch handler make_list"));
        }
    }

    bool is_round_end(){
        return m_state.length() % 2 == 0;
    }

    elv_return_type do_challenge(BitCodeCallContext* ctx, JPCParams& p){
        try{
            auto qp = ctx->QueryParams(p);
            if (qp.second.IsError()){
                return ctx->make_error("getting Query Params", qp.second);
            }
            auto it_state = qp.first.find("state");
            LOG_INFO(ctx, "do_challenge entered", "state", qp.first);
            if(it_state != qp.first.end()){
                auto it_vote = qp.first.find("vote");
                int vote = -1;
                if (it_vote != qp.first.end()){
                    auto vote_string = it_vote->second;
                    vote = std::atoi(vote_string.c_str());
                }
                auto entries = ctx->SQMDGetJSON("/");
                if (entries.second.IsError()){
                    return ctx->make_error("unable to locate game entries", entries.second);
                }

                auto entrants_pair_a = make_list(ctx, entries.first, "bracket-snoop-a");
                if (entrants_pair_a.second.IsError()){
                    return ctx->make_error("making entrants array", E(entrants_pair_a.second));
                }
                auto entrants_pair_b = make_list(ctx, entries.first, "bracket-snoop-b");
                if (entrants_pair_b.second.IsError()){
                    return ctx->make_error("making entrants array", E(entrants_pair_b.second));
                }
                auto rounds_pair = make_list(ctx, entries.first, "bracket-snoop-rounds");
                if (rounds_pair.second.IsError()){
                    return ctx->make_error("making rounds array", E(rounds_pair.second));
                }
                auto winners_pair = make_list(ctx, entries.first, "bracket-snoop-winners");
                if (rounds_pair.second.IsError()){
                    return ctx->make_error("making rounds array", E(rounds_pair.second));
                }
                auto rounds = rounds_pair.first;
                m_base = entrants_pair_a.first.size();
                m_state = it_state->second; // current cookie
                if (m_state == "" && vote == -1){ // new game
                    m_is_round_change = true;
                    m_round = 0;
                    return urls(ctx, 0, 1, entrants_pair_a.first, entrants_pair_b.first, rounds_pair.first);
                }
                const int max_len = max_length();
                auto digits_per_entrant = ilog16(m_base); // encoding for 1 enrty in the cookie
                adjust_state(vote, digits_per_entrant);
                auto state_len = (int32_t)m_state.length()/digits_per_entrant;
                auto cur_bracket = find_bracket(state_len);
                m_is_round_change = cur_bracket.second == 0;
                m_round = cur_bracket.first;
                LOG_INFO(ctx, "State", "value", m_state);
                if (state_len == max_len){
                    char c = m_state.at(m_state.length()-1);
                    int winner_index;
                    std::stringstream ss;
                    ss << c;
                    ss >> std::hex >> winner_index;
                    if (winner_index >= m_base){
                        return ctx->make_error("winner index out of range", E("winner index out of range"));
                    }
                    auto asset_meta_winner = winners_pair.first[winner_index]["/"].get<std::string>();
                    const auto qfab_pre = "qfab/";
                    auto qfabLoc0 = asset_meta_winner.find(qfab_pre);
                    std::string lookup = "/public/asset_metadata/playlists/bracket-snoop-winners/list/";

                    lookup += winners_pair.first[winner_index]["key"];
                    lookup += "/display_title";;

                    auto pr = ctx->SQMDGetJSONResolve(lookup.c_str());
                    if (pr.second.IsError()){
                        return ctx->make_error("unable to find meta video0", pr.second);
                    }
                    std::string label = pr.first;

                    auto hash_winner = asset_meta_winner.substr(qfabLoc0 + strlen(qfab_pre), asset_meta_winner.find("/", qfabLoc0 + strlen(qfab_pre))-(qfabLoc0 + strlen(qfab_pre)));
                    LOG_INFO(ctx, "WINNER", "val", winners_pair.first[winner_index]["/"]);
                    std::stringstream  oss;
                    oss    << "{\"videos\" : [";
                    insert_video(oss, pr.first, "winner", hash_winner);
                    oss         << "],\"state\":\"" << m_state << "\"}";
                    std::vector<unsigned char> json_bytes;
                    copy(istream_iterator<unsigned char>(oss), istream_iterator<unsigned char>(), back_inserter(json_bytes));
                    auto p = ctx->Callback(200, "application/json", json_bytes.size());
                    if (p.second.IsError()){
                        return ctx->make_error("callback failure", p.second);
                    }
                    p = ctx->WriteOutput(json_bytes);
                    if (p.second.IsError()){
                        return ctx->make_error("writeoutput failure", p.second);
                    }

                    return ctx->make_success(nlohmann::json::parse(oss.str()));
                }
                auto parents = get_parents(ctx, cur_bracket);
                return urls(ctx, parents.first, parents.second, entrants_pair_a.first, entrants_pair_b.first, rounds_pair.first);
            }
            return ctx->make_error("no state token", E("no state token"));
        }
        catch (json::exception& e){
            return ctx->make_error("do_challenge excpetion occured", E(e.what()));
        }
        catch (std::exception &e) {
            return ctx->make_error("do_challenge std excpetion occured", e.what());
        }
        catch(...){
            return ctx->make_error("do_challenge ... excpetion occured", E("elipsis catch handler make_entrants"));
        }
    }
    ChallengeEngine(){
        m_is_round_change = false;
    }

protected:
    string  m_state;
    int32_t m_base;
    bool    m_is_round_change;
    int     m_round;

};

elv_return_type content(BitCodeCallContext* ctx, JPCParams& p){
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("getting path from JSON", path.second);
    }

    char* request = (char*)(path.first.c_str());

    ChallengeEngine ce;

    if (strcmp(request, "/challenge") == 0)  // really need to return error if not matching any
      return ce.do_challenge(ctx, p);

    else{
        const char* msg = "unknown  service requested must be /challenge";
        return ctx->make_error(msg, E(msg).Kind(E::Invalid));
    }
}

int cddl_num_mandatories = 4;
char *cddl = (char*)"{"
    "\"title\" : bytes,"
    "\"description\" : text,"
    "\"video\" : eluv.video,"
    "\"image\" : eluv.img"
    "}";

/*
 * Validate content components.
 *
 * Returns:
 *  -1 in case of unexpected failure
 *   0 if valid
 *  >0 the number of validation problems (i.e. components missing or wrong)
 */
elv_return_type validate(BitCodeCallContext* ctx, JPCParams& p){
    int found = cddl_parse_and_check(ctx, cddl);

    char valid_pct[4];
    sprintf(valid_pct, "%d", (uint16_t)(found * 100)/cddl_num_mandatories);

    std::vector<uint8_t> vec(valid_pct, valid_pct + 4);
    auto res = ctx->WriteOutput(vec);
    if (res.second.IsError()){
        return ctx->make_error("write output failed", res.second);
    }
    return ctx->make_success();
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(validate)
END_MODULE_MAP()