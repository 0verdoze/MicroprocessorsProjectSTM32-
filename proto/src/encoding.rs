use std::io::{Write, Error};

pub const BEGIN_FRAME_BYTE: u8 = crate::Frame::BEGIN_FRAME_BYTE;
pub const END_FRAME_BYTE: u8 = crate::Frame::END_FRAME_BYTE;
pub const ESCAPE_BYTE: u8 = 0x1B;

pub const ESCAPE_TABLE: &[(u8, [u8; 2])] = &[
    (ESCAPE_BYTE, [ESCAPE_BYTE, 0x41]),
    (BEGIN_FRAME_BYTE, [ESCAPE_BYTE, 0x42]),
    (END_FRAME_BYTE, [ESCAPE_BYTE, 0x43]),
];


#[derive(Debug, thiserror::Error)]
pub enum DecodeError {
    #[error("invalid escape sequence {0:x?}")]
    InvalidEscapeSequence([u8; 2]),
    #[error("unexpected EOF while decoding (escape byte with no trailing data found)")]
    UnexpectedEOF,
    #[error("{0:}")]
    IOError(#[from] Error),
}

/// Trait implementing encoding and decoding for protocol
pub trait Encoding {
    fn encode(&mut self, data: &[u8]) -> Result<usize, Error>;
    fn decode(&mut self, data: &[u8]) -> Result<usize, DecodeError>;
}

impl<T> Encoding for T 
where
    T: Write,
{
    fn encode(&mut self, data: &[u8]) -> Result<usize, Error> {
        let mut written = 0;

        for byte in data {
            let slice = encode(byte);
            self.write_all(slice)?;

            written += slice.len();
        }

        Ok(written)
    }

    fn decode(&mut self, data: &[u8]) -> Result<usize, DecodeError> {
        let mut written = 0;
        let mut windows = data.windows(2);

        while let Some(window) = windows.next() {
            let (consumed, byte) = decode(window)?;

            self.write_all(std::slice::from_ref(&byte))?;
            written += consumed;

            (0..consumed.saturating_sub(1))
                .for_each(|_| { windows.next(); })
        }

        if let Some(b) = data.last() {
            let (consumed, byte) = decode(std::slice::from_ref(b))?;

            self.write_all(std::slice::from_ref(&byte))?;
            written += consumed;
        }

        Ok(written)
    }
}

#[inline]
fn encode<'a>(b: &'a u8) -> &'a [u8] {
    ESCAPE_TABLE.iter()
        .find_map(|(d, e)| {
            (d == b).then_some(e.as_slice())
        }).unwrap_or(std::slice::from_ref(b))
}

#[inline]
fn decode(window: &[u8]) -> Result<(usize, u8), DecodeError> {
    if window[0] == ESCAPE_BYTE {
        if window.len() > 1 {
            ESCAPE_TABLE.iter()
                .find_map(|(d, e)| (e[1] == window[1]).then_some((2usize, *d)))
                .ok_or(DecodeError::InvalidEscapeSequence([window[0], window[1]]))
        } else {
            Err(DecodeError::UnexpectedEOF)
        }
    } else {
        Ok((1, window[0]))
    }
}
