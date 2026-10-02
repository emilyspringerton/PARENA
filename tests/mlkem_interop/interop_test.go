package interop

import (
	"bytes"
	"crypto/mlkem"
	"encoding/hex"
	"os/exec"
	"strings"
	"testing"
)

func c(t *testing.T, args ...string) []string {
	out, err := exec.Command("./c/cli", args...).Output()
	if err != nil {
		t.Fatal(err)
	}
	return strings.Fields(string(out))
}

// C keygen -> Go encapsulates to C's key -> C decapsulates; shared secrets must match.
func TestCKeyGoEncaps(t *testing.T) {
	for i := 0; i < 50; i++ {
		kg := c(t, "keygen")
		pkb, _ := hex.DecodeString(kg[0])
		ek, err := mlkem.NewEncapsulationKey768(pkb)
		if err != nil {
			t.Fatalf("Go rejected C public key: %v", err)
		}
		ss, ct := ek.Encapsulate()
		got := c(t, "decaps", kg[1], hex.EncodeToString(ct))
		if got[0] != hex.EncodeToString(ss) {
			t.Fatalf("iter %d: shared secret mismatch", i)
		}
	}
}

// Go keygen -> C encapsulates -> Go decapsulates.
func TestGoKeyCEncaps(t *testing.T) {
	for i := 0; i < 50; i++ {
		dk, _ := mlkem.GenerateKey768()
		out := c(t, "encaps", hex.EncodeToString(dk.EncapsulationKey().Bytes()))
		ct, _ := hex.DecodeString(out[0])
		ss, err := dk.Decapsulate(ct)
		if err != nil || hex.EncodeToString(ss) != out[1] {
			t.Fatalf("iter %d: mismatch (%v)", i, err)
		}
	}
}

// Implicit rejection: a tampered ciphertext must NOT yield the real secret.
func TestTamperedCiphertextRejected(t *testing.T) {
	kg := c(t, "keygen")
	pkb, _ := hex.DecodeString(kg[0])
	ek, _ := mlkem.NewEncapsulationKey768(pkb)
	ss, ct := ek.Encapsulate()
	ct[5] ^= 1
	got := c(t, "decaps", kg[1], hex.EncodeToString(ct))
	if bytes.Equal([]byte(got[0]), []byte(hex.EncodeToString(ss))) {
		t.Fatal("tampered ciphertext produced the real shared secret")
	}
}
