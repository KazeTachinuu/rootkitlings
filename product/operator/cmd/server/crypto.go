package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/hkdf"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
)

var errCiphertextShort = errors.New("ciphertext too short")

type OperatorKeys struct {
	priv *ecdh.PrivateKey
	pub  *ecdh.PublicKey
	dir  string
}

func LoadOperatorKeys(dir string) (*OperatorKeys, error) {
	curve := ecdh.X25519()
	privBytes, err := os.ReadFile(filepath.Join(dir, "operator.key"))
	if err != nil {
		return nil, fmt.Errorf("read operator key: %w", err)
	}
	priv, err := curve.NewPrivateKey(privBytes)
	if err != nil {
		return nil, fmt.Errorf("parse operator key: %w", err)
	}
	return &OperatorKeys{
		priv: priv,
		pub:  priv.PublicKey(),
		dir:  dir,
	}, nil
}

func (k *OperatorKeys) PubBytes() []byte { return k.pub.Bytes() }

// DeriveAgent performs ECDH with an agent's public key and returns the agent ID and shared key.
func (k *OperatorKeys) DeriveAgent(agentPub []byte) (string, [32]byte, error) {
	curve := ecdh.X25519()
	pub, err := curve.NewPublicKey(agentPub)
	if err != nil {
		return "", [32]byte{}, err
	}
	secret, err := k.priv.ECDH(pub)
	if err != nil {
		return "", [32]byte{}, err
	}
	keyBytes, err := hkdf.Key(sha256.New, secret, nil, "rklings-aead", 32)
	if err != nil {
		return "", [32]byte{}, err
	}
	var sharedKey [32]byte
	copy(sharedKey[:], keyBytes)
	idBytes, err := hkdf.Key(sha256.New, secret, nil, "rklings-id", 8)
	if err != nil {
		return "", [32]byte{}, err
	}
	return hex.EncodeToString(idBytes), sharedKey, nil
}

func sealAEAD(key [32]byte, plaintext []byte) ([]byte, error) {
	gcm, err := newGCM(key)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, gcm.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return nil, err
	}
	return gcm.Seal(nonce, nonce, plaintext, nil), nil
}

func openAEAD(key [32]byte, ciphertext []byte) ([]byte, error) {
	gcm, err := newGCM(key)
	if err != nil {
		return nil, err
	}
	if len(ciphertext) < gcm.NonceSize() {
		return nil, errCiphertextShort
	}
	return gcm.Open(nil, ciphertext[:gcm.NonceSize()], ciphertext[gcm.NonceSize():], nil)
}

func newGCM(key [32]byte) (cipher.AEAD, error) {
	block, err := aes.NewCipher(key[:])
	if err != nil {
		return nil, err
	}
	return cipher.NewGCM(block)
}
