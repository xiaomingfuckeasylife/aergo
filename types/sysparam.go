/**
 *  @file sysparam.go
 *  @copyright defined in aergo/LICENSE.txt
 */
package types

import "strconv"

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
func NewSpInt64(name, desc, val string, mut, dao bool) *SpInt64 {
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

// SetString sets i to the integer converted from v.
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

// GetBytes returns the marshaled byte array of *i.
func (i *SpInt64) GetBytes() []byte {
	return []byte(i.GetValue())
}

// SpStr implements SpValue where the parameter value is a string.
type SpStr string

// SetString sets i to the integer converted from v.
func (s SpStr) SetString(v string) error {
	s = SpStr(v)

	return nil
}

// Get returns *i.
func (s SpStr) Get() interface{} {
	return string(s)
}

// SetBytes sets *i to b.
func (s SpStr) SetBytes(b []byte) error {
	s = SpStr(b)

	return nil
}

// GetBytes returns the marshaled byte array of *i.
func (s SpStr) GetBytes() []byte {
	return []byte(s)
}
