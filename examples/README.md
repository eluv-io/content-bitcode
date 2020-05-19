![Eluvio Logo](static/images/Logo-Small.png "Eluvio Logo")

# Eluvio Bitcode Samples

There are a number of samples included in the tree. Located at [bitcode/include/eluvio](../bitcode/include/eluvio) wherein most of the directories contain standalone fabric bitcode modules.

One file in particular [bitcode_content.h](../bitcode/include/eluvio/bitcode_context.h), contains the full description and communication mechanism of the LLVM bitcode extensibility layer for the fabric. For
information about the bitcode API see [bitcode readme](../README.md)

# Creating a bracket based game.

- Create bitcode module to serve game
- Create bitcode class to hold and navigate state
- Provide calling interface for client
- Provide json response


# The bracket game

The bitcode layer provides a mechanism to extend the content fabric and provide new utility on top of the distributed nature of the fabric.  One example is to create a bracket game using the fabric's ability to store, retrieve, and link content metadata.  In this game, the user will interact with the game using the a front end that will provide a left and right question.  In effect this gives the user the choice between 2 image questions.  The UI for the game asks the user to choose between the 2 possibilities and submit a vote.  The game is implemented in a way that the vote and the previous state are trasmitted in the http request, and the response containing the next 2 choices will also include the previous state. The particular implementation that will be discussed handles a 16 entry bracket game.  For round one, there will be sixteen entrants, for round two there will be 8 entrants.  The process will continue until a winner is determined.

The design for this state machine is fairly straighforward in that the game will be called using an http request containing query params with a state string and vote (0/1).  The state is a linearized sequence of previous winners of the votes.  Thus a state of "134" would indicate the current vote is in round 1 and the winners of the first three slots are locations 1, 3, and 4 of the initial 16 bracket *note the array is 0 based*.  A state of "13469ACF3" would be in round 2 as "13469ACF" would be the first round's results *note there are 8*, and 3 would indicate that 3 defeated 1 in the beginning of the second round.


Below is a detailed explanation of how this example works.

# Base module

In order to begin to write the bracket challenge sample (the game), we need to author a bare bones callable piece of content bitcode. *If you have not already, please review [bitcode readme](../README.md)*.


Here is a base module:
```c++
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

elv_return_type content(BitCodeCallContext* ctx, JPCParams& p){
    const char* msg = "Nothing done yet";
    return ctx->make_error(msg, E(msg).Kind(E::Invalid));
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
```
 The key takeaway is the function **content** and the **MODULE_MAP**. Both concepts can be found in the [bitcode readme](../README.md).

## Creating a state object

The basis of the games is providing 2 choices and remembering where in the game the player currently is.  Below is a functional view of the challenge engine or the state machine.  Note the state data amounts to a string that will never be longer that 16 and a few other ancillary pieces of data for convenience.  The main entry point for the game is the do_challenge method of the engine and that will be wired up directly to the content entry point *described above*.

```c++
class ChallengeEngine {
public:
    typedef std::pair<int32_t,int32_t> bracket_type;

    static inline uint32_t ilog2(const uint32_t x);

    static unsigned int ilog16 (unsigned int val);

    inline int32_t value_at_pos(int32_t pos, int32_t digits_per);

    bracket_type get_parents(BitCodeCallContext* ctx,
                             bracket_type& child);

    std::string make_url(std::string hash, bool is_dash = true);

    void insert_video(std::stringstream& oss,
                      string label,
                      string id,
                       string hash);

    elv_return_type urls(BitCodeCallContext* ctx,
                         int video0,
                         int video1,
                         nlohmann::json& entries_a,
                         nlohmann::json& entries_b,
                         nlohmann::json& rounds);

    int max_length();

    static bool compare_elements(nlohmann::json l,
                                 nlohmann::json r);

    // turning a json list into an array
    elv_return_type make_list(BitCodeCallContext* ctx,
                              nlohmann::json& entriesList,
                              const char* list);

    bool is_round_end();

    elv_return_type do_challenge(BitCodeCallContext* ctx,
                                 JPCParams& p);

    ChallengeEngine();

protected:
    string  m_state;
    int32_t m_base;
    bool    m_is_round_change;
    int     m_round;

};

```


## Wiring up the engine

Connecting the new engine to the exisiting wiring of the base bitcode module we will end up with an unchanged MODULE_MAP
```c++

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(validate)
END_MODULE_MAP()

```

along with a new implementation of the content function

```c++
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
```

Notice the content function is almost empty as most of the work for this bitcode module is within the challenge engine. Note that variable path above (**auto path;**) is a
```c++
typedef std::pair<nlohmann::json,E> elv_return_type;
```
elv_return_type is a std::pair<F,S> of [nlohmann::json type](https://nlohmann.github.io/json/) and an eluvio [E type](../bitcode/include/eluvio/error.h). **It is important to remember that all interactions with the fabric return this pair type.**  The result is always the **first** element of the std::pair and is of type **nlohmann::json**.  The error is always the **second** element of the pair and is of type **eluvio_errors::Error**.


## Exploring the challenge engine

The focual point of the bitcode module in this case is the challenge engine.  In particular the do_challenge method is that heart of the engine.

```c++
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
//         CODE REMOVED FOR BREVITY
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
```

As this method is called directly by the **content** function, the 2 parameters **ctx** and **p** are the elements of the http request from the game player.  At the beginning of the method we call a helper method on the BitCodeCallContext instance to get the players query params from the http request.  After checking the pair for error the code then goes on to discover state and a potential vote/choice.  The game always begins with an empty state and no vote present.

The remainder of the game from a functional view is to decode the current state to determine which game and round as well as potential contestants.  Once the contestants are located by index, the games proceeds to acquire the video playout details of the contestants from the content object's meta data.

## About the meta data

As the game details are stored in the current content objects meta data, the game looks in the current object at root (/) to acquire the game data.  This data accomodates having 2 brackets of the same contestants.  Thus bracket A will have 16 contestants with one set of videos, brackets b will again have 16 of the same contestants but with a different playour URL. All of this info is stored in the content objects metadata at its root.

```c++
    auto entries = ctx->SQMDGetJSON("/");
    if (entries.second.IsError()){
        return ctx->make_error("unable to locate game entries", entries.second);
    }
    // entries now has the contestant entries for both brackets
```

From here the code creates a unified list of all playout options for easier retrieval using the engine method **make_list**.  Once the lists are formed, the bitcode proceeds to create the playout URL utilizing fabric meta links to resolve the actual playout in another content object.

```c++
    elv_return_type urls(BitCodeCallContext* ctx, int video0, int video1, nlohmann::json& entries_a, nlohmann::json& entries_b,nlohmann::json& rounds){
        try{
            // code removed for brevity soo challenge.cpp for full
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
```

The snippet of the method urls, demonstrates how we use the fabric method of **SQMDGetJSONResolve** to have the fabric resolve meta data links.

## Fabric Response

Creating the fabric response is relatively simple thanks in part to the BitCodeCallContext's make_error and make_success methods.  They each take a respective error oo nlohmann::json for input.  In the case of success, we merely return a json result set consisting of the state and a new pair of contestants.  In the case of a round change, (*eg round of 16 to round of 8*) the result would have the new round video to enable transition.  Finally, at the end there can be only one winner which is announced in its own json result.  The method **urls** handles all of the results except for the winner which is calculated in do_challenge itself.

below is a snippet from the urls method where the rsult json is calculated and returned.
```c++
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
```
The code above create a std::stringstream or a stream based on a string.  The operator `<<` provides a convenient mechanism to add new elements to the stream.  Note the strings are streamed in order. The stream is then converted to a memory buffer which is then written back to the fabric.  The WriteOutput method on the BitCodeCallContext ensures that all output is directed appropriately to the http response.

## Next Steps

At this point you should have some idea of what Eluvio's Content Fabric bitcode API looks like.  You are familiar with the details of how to author and wire up an http request on a content object that provides a stateful game to an http consumer.  We encourage you to explore some of the other bitcode modules we have provided as they demonstrate a wide range of problems and solutions.

[static video/audio](../bitcode/avmaster/avmaster2000.cpp)

[live video/audio](../bitcode/avlive/live_rtp.cpp)

[http/proxy](../bitcode/proxy/proxy.cpp)

[lots more](../bitcode/)

