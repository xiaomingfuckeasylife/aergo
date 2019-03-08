package system

import (
	"bytes"
	"errors"
	"math/big"

	"github.com/aergoio/aergo/state"
	"github.com/aergoio/aergo/types"
	"github.com/mr-tron/base58"
)

type VoteResult struct {
	rmap map[string]*big.Int
	key  []byte
	ex   bool
}

func newVoteResult(key []byte) *VoteResult {
	voteResult := &VoteResult{}
	voteResult.rmap = map[string]*big.Int{}
	if bytes.Equal(key, defaultVoteKey) {
		voteResult.ex = false
	} else {
		voteResult.ex = true
	}
	voteResult.key = key
	return voteResult
}

func (voteResult *VoteResult) SubVote(vote *types.Vote) {
	for offset := 0; offset < len(vote.Candidate); offset += PeerIDLength {
		peer := vote.Candidate[offset : offset+PeerIDLength]
		pkey := base58.Encode(peer)
		voteResult.rmap[pkey] = new(big.Int).Sub(voteResult.rmap[pkey], vote.GetAmountBigInt())
	}
}

func (voteResult *VoteResult) AddVote(vote *types.Vote) {
	for offset := 0; offset < len(vote.Candidate); offset += PeerIDLength {
		key := vote.Candidate[offset : offset+PeerIDLength]
		if voteResult.rmap[base58.Encode(key)] == nil {
			voteResult.rmap[base58.Encode(key)] = new(big.Int).SetUint64(0)
		}
		voteResult.rmap[base58.Encode(key)] = new(big.Int).Add(voteResult.rmap[base58.Encode(key)], vote.GetAmountBigInt())
	}
}

func (vr VoteResult) Sync(scs *state.ContractState) error {
	voteList := buildVoteList(vr.rmap)
	return scs.SetData(append(sortKey, vr.key...), serializeVoteList(voteList, vr.ex))
}

func loadVoteResult(scs *state.ContractState, key []byte) (*VoteResult, error) {
	data, err := scs.GetData(append(sortKey, key...))
	if err != nil {
		return nil, err
	}
	voteResult := newVoteResult(key)
	if len(data) != 0 {
		voteList := deserializeVoteList(data, voteResult.ex)
		if voteList != nil {
			for _, v := range voteList.GetVotes() {
				if voteResult.ex {
					voteResult.rmap[string(v.Candidate)] = v.GetAmountBigInt()
				} else {
					voteResult.rmap[base58.Encode(v.Candidate)] = v.GetAmountBigInt()
				}
			}
		}
	}
	return voteResult, nil
}

func InitVoteResult(scs *state.ContractState, voteResult map[string]*big.Int) error {
	if voteResult == nil {
		return errors.New("Invalid argument : voteReult should not nil")
	}
	res := newVoteResult(defaultVoteKey)
	res.rmap = voteResult
	return res.Sync(scs)
}

func getVoteResult(scs *state.ContractState, key []byte, n int) (*types.VoteList, error) {
	data, err := scs.GetData(append(sortKey, key...))
	if err != nil {
		return nil, err
	}
	var ex bool
	if bytes.Equal(key, defaultVoteKey) {
		ex = false
	} else {
		ex = true
	}
	voteList := deserializeVoteList(data, ex)
	if n < len(voteList.Votes) {
		voteList.Votes = voteList.Votes[:n]
	}
	return voteList, nil
}

func GetVoteResultEx(ar AccountStateReader, key []byte, n int) (*types.VoteList, error) {
	scs, err := ar.GetSystemAccountState()
	if err != nil {
		return nil, err
	}
	return getVoteResult(scs, key, n)
}
