pub struct Rc4 {
    s: [u8; 256],
    i: u8,
    j: u8,
}

impl std::fmt::Debug for Rc4 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Rc4").finish_non_exhaustive()
    }
}

impl Rc4 {
    pub fn new(key: &[u8]) -> Self {
        let mut s = [0u8; 256];
        for i in 0..256 { s[i] = i as u8; }
        let mut j: u8 = 0;
        for i in 0..256 {
            j = j.wrapping_add(s[i]).wrapping_add(key[i % key.len()]);
            s.swap(i, j as usize);
        }
        Self { s, i: 0, j: 0 }
    }

    pub fn apply(&mut self, data: &mut [u8]) {
        for byte in data.iter_mut() {
            self.i = self.i.wrapping_add(1);
            self.j = self.j.wrapping_add(self.s[self.i as usize]);
            self.s.swap(self.i as usize, self.j as usize);
            let k = self.s[self.s[self.i as usize].wrapping_add(self.s[self.j as usize]) as usize];
            *byte ^= k;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypt_decrypt_round_trip() {
        let key = b"test_key_123";
        let original = b"hello world, this is a test message for RC4";
        let mut encrypted = original.to_vec();
        Rc4::new(key).apply(&mut encrypted);
        assert_ne!(&encrypted, original);
        Rc4::new(key).apply(&mut encrypted);
        assert_eq!(&encrypted, original);
    }

    #[test]
    fn different_keys_produce_different_output() {
        let mut a = b"same_input".to_vec();
        let mut b = a.clone();
        Rc4::new(b"key_a").apply(&mut a);
        Rc4::new(b"key_b").apply(&mut b);
        assert_ne!(a, b);
    }

    #[test]
    fn empty_input() {
        let mut data = vec![];
        Rc4::new(b"key").apply(&mut data);
        assert!(data.is_empty());
    }
}
