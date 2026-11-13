//go:build race

package trackparse

// raceEnabled is true under -race, which makes the code several times slower: the property
// tests run fewer cases and the time ceilings are wider.
const raceEnabled = true
