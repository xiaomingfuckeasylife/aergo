/**
 *  @file sysparam.go
 *  @copyright defined in aergo/LICENSE.txt
 */
package config

import "strconv"

// SysParam is a type for system parameter.
type SysParam struct {
	name  string
	desc  string
	mut   bool
	value SpValue
}

// NewSysParam returns a new SysParam.
func NewSysParam(name string, desc string, mut bool, spv SpValue) *SysParam {
	return &SysParam{
		name:  name,
		desc:  desc,
		mut:   mut,
		value: spv,
	}
}

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
type SpInt64 int64

// SetString sets i to the integer converted from v.
func (i *SpInt64) SetString(v string) error {
	x, err := strconv.ParseInt(v, 10, 64)
	*i = SpInt64(x)

	return err
}

// Get returns *i.
func (i *SpInt64) Get() interface{} {
	return *i
}

// SetBytes sets *i to b.
func (i *SpInt64) SetBytes(b []byte) error {
	return i.SetString(string(b))
}

// GetBytes returns the marshaled byte array of *i.
func (i *SpInt64) GetBytes() []byte {
	return []byte(strconv.FormatInt(int64(*i), 64))
}
