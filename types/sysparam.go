/**
 *  @file sysparam.go
 *  @copyright defined in aergo/LICENSE.txt
 */
package types

import (
	"fmt"
	"math/big"
	"strconv"

	"github.com/aergoio/aergo/internal/enc"
)

const (
	// Below are the (enum) constants for the (some) system parameters.

	// SpBpNumber represents the number of BPs.
	SpBpNumber = iota
	// SpGasPrice represents the gas price parameter.
	SpGasPrice
	// SpNamePrice represents the price per name creation.
	SpNamePrice
	// SpMinStake represents the minimum stake, under which staking is not
	// allowed.
	SpMinStake
)

// SpValue is an interface, which is an abstraction of system parameter values.
type SpValue interface {
	// Set sets the parmeter value to v.
	SetString(v string) error
	// Get returns the converted value (for example, int16, int32, int64, ...).
	Get() interface{}

	// SetBytes sets the parameter value to b.
	SetBytes(b []byte) error
	// GetBytes returns the marshaled byte array of the parameter value.
	GetBytes() []byte
}

// SpInt64 implements SpValue where the parameter value is a 64 bit signed
// interger (int64).
type SpInt64 struct {
	*SysParam
	i64Value int64
	cached   bool
}

// NewSpInt64 returns a new SpInt64.
func NewSpInt64(name, desc string, mut, dao bool, val string) *SpInt64 {
	v := &SpInt64{
		SysParam: &SysParam{
			Name: name,
			Desc: desc,
			Mut:  mut,
			Dao:  dao,
		},
	}
	if err := v.SetString(val); err != nil {
		return nil
	}

	return v
}

// SetString iniailizes i with v.
func (i *SpInt64) SetString(v string) error {
	var err error

	if i.i64Value, err = strconv.ParseInt(v, 10, 64); err == nil {
		i.Value = v
		i.cached = true
	}

	return err
}

// Get returns *i.
func (i *SpInt64) Get() interface{} {
	if i.cached {
		return i.i64Value
	}

	var err error
	if i.i64Value, err = strconv.ParseInt(i.GetValue(), 10, 64); err != nil {
		return nil
	}

	i.cached = true

	return i.i64Value
}

// SetBytes sets *i to b.
func (i *SpInt64) SetBytes(b []byte) error {
	return i.SetString(string(b))
}

// SpBigInt implements SpValue where the parameter value is a big interger.
type SpBigInt struct {
	*SysParam
	bi     *big.Int
	cached bool
}

// NewSpBigInt returns a new SpBigInt.
func NewSpBigInt(name, desc string, mut, dao bool, val string) *SpInt64 {
	v := &SpInt64{
		SysParam: newSysParam(name, desc, mut, dao),
	}
	if err := v.SetString(val); err != nil {
		return nil
	}

	return v
}

// SetString initializes bi to v.
func (p *SpBigInt) SetString(v string) error {
	if z, ok := new(big.Int).SetString(v, 10); ok {
		p.Value = v
		p.bi = z
		p.cached = true
		return nil
	}
	return fmt.Errorf("failed to covert %v to a big int", v)
}

// Get returns p.bi
func (p *SpBigInt) Get() interface{} {
	if p.cached {
		return p.bi
	}

	var ok bool
	if p.bi, ok = new(big.Int).SetString(p.GetValue(), 10); !ok {
		return nil
	}

	p.cached = true

	return p.bi
}

// SetBytes initializes p with b.
func (p *SpBigInt) SetBytes(b []byte) error {
	if p.bi = new(big.Int).SetBytes(b); p.bi != nil {
		p.Value = string(b)
		p.cached = true
	}
	return fmt.Errorf("failed to convert %v to a big.Int value", enc.ToString(b))
}

// GetBytes returns a byte representation of p.bi.
func (p *SpBigInt) GetBytes(b []byte) []byte {
	return p.bi.Bytes()
}

// NewSysParam returns a new SysParam.
func newSysParam(name, desc string, mut, dao bool) *SysParam {
	return &SysParam{
		Name: name,
		Desc: desc,
		Mut:  mut,
		Dao:  dao,
	}
}

// GetBytes returns sp.Value.
func (sp *SysParam) GetBytes() []byte {
	return []byte(sp.GetValue())
}
