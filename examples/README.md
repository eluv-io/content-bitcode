![Eluvio Logo](static/images/Logo-Small.png "Eluvio Logo")

# Eluvio Video Bitcode Samples

There are a number of samples included in the tree. Located at [bitcode/include/eluvio](../bitcode/include/eluvio) wherein all of the directories contain standalone fabric bitcode modules.

One file in particular [bitcode_content.h](../bitcode/include/eluvio/bitcode_context.h), contains the full description and communication mechanism of the LLVM bitcode extensibility layer for the fabric. For
information about the bitcode API see [bitcode readme](../README.md)

## Quick Start - Creating a bracket based game.

- Create bitcode module to serve game
- Create bitcode class to hold state
- Provide calling interface for client
- Provide json response


### Basic Example - Step by Step

The bitcode layer is intended to provide a mechanism to extend the
content fabric and provide new utility on top of the distributed nature
of the fabric.  Once such example is to create a bracket game using the
fabric's ability to store, retrieve, and link content metadata.  In this game, the user will interact with the game using the a front end that will
provide a left and right question.  In effect this gives the user the choice between 2 image questions.  The UI for the game asks the user to choose between the 2 possibilities and submit a vote.  The game is implemented in a way that the vote and the previous state are trasmitted
in the http request, and the response containing the next 2 choices will also include the previous state. The particular implementation that will discussed handles a 16 entry bracket game.  For round one, there will be sixteen entrants, for round two there will be 8 entrants.  The process will continue until a winner is determined.

The design for this state machine is fairly straighforward in that the game will be called using and http request containing a query params with a state string and vote (0/1).  The state is a linearized sequence of previous winners of the votes.  Thus a state of "134" would indicate the current vote is in round 1 and the winners of the first three slots are locations 1, 3, and 4 of the initial 16 bracket.  A state of "13469ACF3" would be in round 2 as "13469ACF" would be the first rounds results <em>note there are 8</em>, and 3 would indicate that 3 defeated 1 in the beginning of the second round.


Below is a detailed explanation of how this example works.

### Base module

In order to begin to write the bracket challenge sample (the game), we need to author initialiiy a bare bones callable piece of content bitcode.  <em> If you have not already please review [Content Fabric Readme](../README.md)</em>.

Please review the contents of [basemodule](basemodule.cpp).  The key takeaway if the function **content** and the **MODULE_MAP**. Both concepts can be found in the [README](../README.md).

##### Creating a state object

The basis of the games is providing 2 choices and remembering where in the game the player currently is.  Below is a functional view of the challenge engine or the state machine.  Note the state data amounts to a string that will never be longer that 16 and a few other ancillary pieces of data for convenience.  The main entry point for the game of the do_challenge method of the engine and that will be wired up directly to the content entry point *described above*.

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


#### Step 2 - Wiring up the engine

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

Notice the content function is almost empty as most of the work for this bitcode module is within the challenge engine.


##### Exploring the challenge engine

```c++
```

##### Fabric API (deeper dive)


#### Bext Steps

At this point, you should have fast, high quality video in your player, freshly transcoded from the Fabric.

