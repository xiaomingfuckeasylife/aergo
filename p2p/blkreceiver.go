/*
 * @file
 * @copyright defined in aergo/LICENSE.txt
 */

package p2p

import (
	"bytes"
	"github.com/aergoio/aergo/chain"
	"time"

	"github.com/aergoio/aergo/message"
	"github.com/aergoio/aergo/p2p/p2pcommon"
	"github.com/aergoio/aergo/p2p/subproto"
	"github.com/aergoio/aergo/types"
	"github.com/golang/protobuf/proto"
)

// BlocksChunkReceiver is send p2p getBlocksRequest to target peer and receive p2p responses till all requestes blocks are received
// It will send response actor message if all blocks are received or failed to receive, but not send response if timeout expired, since
// syncer actor already dropped wait before.
type BlocksChunkReceiver struct {
	requestID p2pcommon.MsgID

	peer  p2pcommon.RemotePeer
	actor p2pcommon.ActorService

	blockHashes []message.BlockHash
	timeout     time.Time
	finished    bool
	status      receiverStatus

	got            []*types.Block
	offset         int
	senderFinished chan interface{}
}

type receiverStatus int32

const (
	receiverStatusWaiting receiverStatus = iota
	receiverStatusCanceled
	receiverStatusFinished
)

func NewBlockReceiver(actor p2pcommon.ActorService, peer p2pcommon.RemotePeer, blockHashes []message.BlockHash, ttl time.Duration) *BlocksChunkReceiver {
	timeout := time.Now().Add(ttl)
	return &BlocksChunkReceiver{actor: actor, peer: peer, blockHashes: blockHashes, timeout: timeout, got: make([]*types.Block, len(blockHashes))}
}

func (br *BlocksChunkReceiver) StartGet() {
	hashes := make([][]byte, len(br.blockHashes))
	for i, hash := range br.blockHashes {
		hashes[i] = ([]byte)(hash)
	}
	// create message data
	req := &types.GetBlockRequest{Hashes: hashes}
	mo := br.peer.MF().NewMsgBlockRequestOrder(br.ReceiveResp, subproto.GetBlocksRequest, req)
	br.peer.SendMessage(mo)
	br.requestID = mo.GetMsgID()
}

// ReceiveResp must be called just in read go routine
func (br *BlocksChunkReceiver) ReceiveResp(msg p2pcommon.Message, msgBody proto.Message) (ret bool) {
	// cases in waiting
	//   normal not status => wait
	//   normal status (last response)  => finish
	//   abnormal resp (no following resp expected): hasNext is true => cancel
	//   abnormal resp (following resp expected): hasNext is false, or invalid resp data type (maybe remote peer is totally broken) => cancel finish
	// case in status or status
	ret = true
	switch br.status {
	case receiverStatusWaiting:
		br.handleInWaiting(msg, msgBody)
	case receiverStatusCanceled:
		br.ignoreMsg(msg, msgBody)
		return
	case receiverStatusFinished:
		fallthrough
	default:
		return
	}
	return
}

func (br *BlocksChunkReceiver) handleInWaiting(msg p2pcommon.Message, msgBody proto.Message) {
	// consuming request id when timeoutm, no more resp expected (i.e. hasNext == false ) or malformed body.
	// timeout
	if br.timeout.Before(time.Now()) {
		// silently ignore already status job
		br.finishReceiver()
		return
	}
	// responses malformed data will not expectec remained chunk.
	respBody, ok := msgBody.(types.ResponseMessage)
	if !ok || respBody.GetStatus() != types.ResultStatus_OK {
		br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Err: message.RemotePeerFailError})
		br.finishReceiver()
		return
	}
	// remote peer response malformed data.
	body, ok := msgBody.(*types.GetBlockResponse)
	if !ok || len(body.Blocks) == 0 {
		br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Err: message.MissingHashError})
		br.finishReceiver()
		return
	}

	// add to Got
	for _, block := range body.Blocks {
		// It also error that response has more blocks than expected(=requested).
		if br.offset >= len(br.got) {
			br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Blocks: nil, Err: message.TooManyBlocksError})
			br.cancelReceiving(body.HasNext)
			return
		}
		// unexpected block
		if !bytes.Equal(br.blockHashes[br.offset], block.Hash) {
			br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Err: message.UnexpectedBlockError})
			br.cancelReceiving(body.HasNext)
			return
		}
		if proto.Size(block) > int(chain.MaxBlockSize()) {
			br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Err: message.TooBigBlockError})
			br.cancelReceiving(body.HasNext)
			return
		}
		br.got[br.offset] = block
		br.offset++
	}
	// remote peer hopefully sent last chunk
	if !body.HasNext {
		if br.offset < len(br.got) {
			br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Err: message.TooFewBlocksError})
			// not all blocks were filled. this is error
		} else {
			br.actor.TellRequest(message.SyncerSvc, &message.GetBlockChunksRsp{ToWhom: br.peer.ID(), Blocks: br.got, Err: nil})
		}
		br.finishReceiver()
	}
	return
}

// cancelReceiving is to cancel receiver the middle in receiving, waiting remaining (and useless) response. It is assumed cancelings are not frequently occur
func (br *BlocksChunkReceiver) cancelReceiving(hasNext bool) {
	br.status = receiverStatusCanceled
	// check time again. since negative duration of timer will not fire channel.
	interval := br.timeout.Sub(time.Now())
	if !hasNext || interval <= 0 {
		// if remote peer will not send partial response anymore. it it actually same as finish.
		br.finishReceiver()
	} else {
		// canceling in the middle of responses
		br.senderFinished = make(chan interface{})
		go func() {
			timer := time.NewTimer(interval)
			select {
			case <-timer.C:
				break
			case <-br.senderFinished:
				break
			}
			br.peer.ConsumeRequest(br.requestID)
		}()
	}
}

// finishReceiver is to cancel works, assuming cancelings are not frequently occur
func (br *BlocksChunkReceiver) finishReceiver() {
	br.status = receiverStatusCanceled
	br.peer.ConsumeRequest(br.requestID)
}

// ignoreMsg is silently ignore following responses, which is not useless anymore.
func (br *BlocksChunkReceiver) ignoreMsg(msg p2pcommon.Message, msgBody proto.Message) {
	body, ok := msgBody.(*types.GetBlockResponse)
	if !ok {
		return
	}
	if !body.HasNext {
		// really status from remote peer
		select {
		case br.senderFinished <- struct{}{}:
		default:
		}
	}
}
