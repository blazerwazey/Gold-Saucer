use thiserror::Error;

/// Errors that can occur while compiling the mini Makou field-script DSL.
#[derive(Debug, Error)]
pub enum FieldCompileError {
    #[error("empty instruction on line {line}")]
    EmptyInstruction { line: usize },

    #[error("unknown opcode '{opcode}' on line {line}")]
    UnknownOpcode { line: usize, opcode: String },

    #[error("wrong argument count for {opcode} on line {line}: expected {expected}, got {got}")]
    WrongArgCount {
        line: usize,
        opcode: String,
        expected: usize,
        got: usize,
    },

    #[error("failed to parse integer '{token}' on line {line}")]
    ParseInt {
        line: usize,
        token: String,
        #[source]
        source: std::num::ParseIntError,
    },

    #[error("value '{token}' on line {line} is out of range for {kind}")]
    ValueOutOfRange {
        line: usize,
        token: String,
        kind: &'static str,
    },
}

fn parse_int_u8(line: usize, token: &str) -> Result<u8, FieldCompileError> {
    let t = token.trim_end_matches(',');
    let res = if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        u8::from_str_radix(hex, 16)
    } else {
        t.parse::<u8>()
    };

    res.map_err(|e| FieldCompileError::ParseInt {
        line,
        token: t.to_string(),
        source: e,
    })
}

fn parse_int_u16(line: usize, token: &str) -> Result<u16, FieldCompileError> {
    let t = token.trim_end_matches(',');
    let res = if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        u16::from_str_radix(hex, 16)
    } else {
        t.parse::<u16>()
    };

    res.map_err(|e| FieldCompileError::ParseInt {
        line,
        token: t.to_string(),
        source: e,
    })
}

fn pack_banks(line: usize, b1: u8, b2: u8) -> Result<u8, FieldCompileError> {
    if b1 > 0x0F {
        return Err(FieldCompileError::ValueOutOfRange {
            line,
            token: format!("{:#X}", b1),
            kind: "bank1 (0-15)",
        });
    }
    if b2 > 0x0F {
        return Err(FieldCompileError::ValueOutOfRange {
            line,
            token: format!("{:#X}", b2),
            kind: "bank2 (0-15)",
        });
    }
    Ok((b1 << 4) | (b2 & 0x0F))
}

/// Compile a small textual DSL into FF7 field-script bytecode.
///
/// Line format (case-insensitive opcodes; decimal or 0x-prefixed hex numbers):
///
/// - STITM banks item_id qty
///   Encodes: [0x58, banks, item_lo, item_hi, qty]
///
/// - SMTRA b1b2 b3b4 materia_id ap0 ap1 ap2
///   or
///   SMTRA materia_id ap0 ap1 ap2   (shorthand for constant form with b1b2=b3b4=0)
///   Encodes: [0x5B, b1b2, b3b4, materia_id, ap0, ap1, ap2]
///
/// - BITON  bank1 bank2 var bit
/// - BITOFF bank1 bank2 var bit
/// - BITXOR bank1 bank2 var bit
///   Encodes: [opcode, (bank1<<4)|bank2, var, bit]
///
/// - MESSAGE window_id text_id
///   Encodes: [0x40, window_id, text_id]
///
/// Blank lines and lines starting with `#` or `//` are ignored.
pub fn compile_script_from_str(src: &str) -> Result<Vec<u8>, FieldCompileError> {
    let mut out = Vec::new();

    for (idx, raw_line) in src.lines().enumerate() {
        let line_no = idx + 1;
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with("//") {
            continue;
        }

        let mut parts = line.split_whitespace();
        let op_tok = match parts.next() {
            Some(tok) => tok,
            None => {
                return Err(FieldCompileError::EmptyInstruction { line: line_no });
            }
        };

        let opcode = op_tok.to_ascii_uppercase();
        let args: Vec<String> = parts.map(|s| s.to_string()).collect();

        match opcode.as_str() {
            "RET" => {
                if args.is_empty() {
                    out.push(0x00);
                } else {
                    return Err(FieldCompileError::WrongArgCount {
                        line: line_no,
                        opcode,
                        expected: 0,
                        got: args.len(),
                    });
                }
            }

            "SETWORD" => {
                // Constant SETWORD: SETWORD bank addr value
                // Encodes: [0x81, (bank<<4)|0, addr, value_lo, value_hi]
                if args.len() != 3 {
                    return Err(FieldCompileError::WrongArgCount {
                        line: line_no,
                        opcode,
                        expected: 3,
                        got: args.len(),
                    });
                }

                let bank = parse_int_u8(line_no, &args[0])?;
                let addr = parse_int_u8(line_no, &args[1])?;
                let value = parse_int_u16(line_no, &args[2])?;

                if bank > 0x0F {
                    return Err(FieldCompileError::ValueOutOfRange {
                        line: line_no,
                        token: format!("{:#X}", bank),
                        kind: "bank (0-15)",
                    });
                }

                let ds = (bank << 4) & 0xF0; // low nibble = 0: constant source
                let [v_lo, v_hi] = value.to_le_bytes();

                out.push(0x81);
                out.push(ds);
                out.push(addr);
                out.push(v_lo);
                out.push(v_hi);
            }

            "STITM" => {
                if args.len() != 3 {
                    return Err(FieldCompileError::WrongArgCount {
                        line: line_no,
                        opcode,
                        expected: 3,
                        got: args.len(),
                    });
                }
                let banks = parse_int_u8(line_no, &args[0])?;
                let item_id = parse_int_u16(line_no, &args[1])?;
                let qty = parse_int_u8(line_no, &args[2])?;

                let [lo, hi] = item_id.to_le_bytes();
                out.push(0x58); // STITM
                out.push(banks);
                out.push(lo);
                out.push(hi);
                out.push(qty);
            }

            "SMTRA" => {
                match args.len() {
                    4 => {
                        // Shorthand: constant form with zero bank bytes.
                        let materia_id = parse_int_u8(line_no, &args[0])?;
                        let ap0 = parse_int_u8(line_no, &args[1])?;
                        let ap1 = parse_int_u8(line_no, &args[2])?;
                        let ap2 = parse_int_u8(line_no, &args[3])?;

                        out.push(0x5B);
                        out.push(0); // b1b2
                        out.push(0); // b3b4
                        out.push(materia_id);
                        out.push(ap0);
                        out.push(ap1);
                        out.push(ap2);
                    }
                    6 => {
                        // Full form.
                        let b1b2 = parse_int_u8(line_no, &args[0])?;
                        let b3b4 = parse_int_u8(line_no, &args[1])?;
                        let materia_id = parse_int_u8(line_no, &args[2])?;
                        let ap0 = parse_int_u8(line_no, &args[3])?;
                        let ap1 = parse_int_u8(line_no, &args[4])?;
                        let ap2 = parse_int_u8(line_no, &args[5])?;

                        out.push(0x5B);
                        out.push(b1b2);
                        out.push(b3b4);
                        out.push(materia_id);
                        out.push(ap0);
                        out.push(ap1);
                        out.push(ap2);
                    }
                    _ => {
                        return Err(FieldCompileError::WrongArgCount {
                            line: line_no,
                            opcode,
                            expected: 4, // or 6; message explains actual count
                            got: args.len(),
                        });
                    }
                }
            }

            "BITON" | "BITOFF" | "BITXOR" => {
                if args.len() != 4 {
                    return Err(FieldCompileError::WrongArgCount {
                        line: line_no,
                        opcode,
                        expected: 4,
                        got: args.len(),
                    });
                }
                let bank1 = parse_int_u8(line_no, &args[0])?;
                let bank2 = parse_int_u8(line_no, &args[1])?;
                let var = parse_int_u8(line_no, &args[2])?;
                let bit = parse_int_u8(line_no, &args[3])?;
                let banks = pack_banks(line_no, bank1, bank2)?;

                let op_byte = match opcode.as_str() {
                    "BITON" => 0x82,
                    "BITOFF" => 0x83,
                    _ => 0x84, // BITXOR
                };

                out.push(op_byte);
                out.push(banks);
                out.push(var);
                out.push(bit);
            }

            "MESSAGE" => {
                if args.len() != 2 {
                    return Err(FieldCompileError::WrongArgCount {
                        line: line_no,
                        opcode,
                        expected: 2,
                        got: args.len(),
                    });
                }
                let window_id = parse_int_u8(line_no, &args[0])?;
                let text_id = parse_int_u8(line_no, &args[1])?;

                out.push(0x40);
                out.push(window_id);
                out.push(text_id);
            }

            _ => {
                return Err(FieldCompileError::UnknownOpcode {
                    line: line_no,
                    opcode,
                });
            }
        }
    }

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::compile_script_from_str;

    #[test]
    fn compiles_basic_stitm() {
        let src = "STITM 0 0x0068 1";
        let bytes = compile_script_from_str(src).unwrap();
        assert_eq!(bytes, vec![0x58, 0x00, 0x68, 0x00, 0x01]);
    }

    #[test]
    fn compiles_setword_constant() {
        let src = "SETWORD 2 0x1C 0xFEFF";
        let bytes = compile_script_from_str(src).unwrap();
        assert_eq!(bytes, vec![0x81, 0x20, 0x1C, 0xFF, 0xFE]);
    }

    #[test]
    fn compiles_constant_smtra_shorthand() {
        let src = "SMTRA 0x31 0 0 0";
        let bytes = compile_script_from_str(src).unwrap();
        assert_eq!(bytes, vec![0x5B, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn compiles_biton() {
        let src = "BITON 1 0 66 4";
        let bytes = compile_script_from_str(src).unwrap();
        assert_eq!(bytes, vec![0x82, 0x10, 66, 4]);
    }

    #[test]
    fn compiles_message() {
        let src = "MESSAGE 0 5";
        let bytes = compile_script_from_str(src).unwrap();
        assert_eq!(bytes, vec![0x40, 0x00, 0x05]);
    }
}
