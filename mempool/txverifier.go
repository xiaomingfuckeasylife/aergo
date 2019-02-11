package mempool

import (
	"github.com/aergoio/aergo-actor/actor"
	log "github.com/aergoio/aergo-lib/log"
	"github.com/aergoio/aergo/message"
	"github.com/aergoio/aergo/types"
	"github.com/golang/protobuf/proto"
)

type TxVerifier struct {
	mp     *MemPool
	logger *log.Logger
}

func NewTxVerifier(p *MemPool, l *log.Logger) *TxVerifier {
	return &TxVerifier{mp: p, logger: l}
}

//Receive actor message
func (s *TxVerifier) Receive(context actor.Context) {
	switch msg := context.Message().(type) {
	case *types.Tx:
		var err error
		if proto.Size(msg) > txMaxSize {
			err = types.ErrTxSizeExceedLimit
		} else if s.mp.exist(msg.GetHash()) != nil {
			err = types.ErrTxAlreadyInMempool
		} else {
			err = s.mp.verifyTx(msg)
			if err == nil {
				err = s.mp.put(msg)
			}
		}
		s.logger.Debug().Str("hash", types.ToTxID(msg.GetHash()).String()).Err(err).
			Str("acc", types.EncodeAddress(msg.GetBody().GetAccount())).Uint64("n", msg.GetBody().GetNonce()).Msg("Mempool put result")
		context.Respond(&message.MemPoolPutRsp{Err: err})
	}
}
