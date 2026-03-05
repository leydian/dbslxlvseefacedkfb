using System;
using System.Collections.Generic;

namespace VsfClone.Xav2.Runtime
{
    public static class Xav2Lz4Codec
    {
        private const int MinMatch = 4;
        private const int HashBits = 16;
        private const int HashSize = 1 << HashBits;
        private const int MaxDistance = 65535;

        public static bool TryCompress(byte[] source, out byte[] compressed, bool preferRatio)
        {
            compressed = Array.Empty<byte>();
            if (source == null)
            {
                return false;
            }
            if (source.Length == 0)
            {
                compressed = Array.Empty<byte>();
                return true;
            }

            var hashTable = new int[HashSize];
            for (var i = 0; i < hashTable.Length; i++)
            {
                hashTable[i] = -1;
            }

            var dst = new List<byte>(source.Length);
            var anchor = 0;
            var pos = 0;

            while (pos + MinMatch <= source.Length)
            {
                var h = Hash32(Read32(source, pos));
                var candidate = hashTable[h];
                hashTable[h] = pos;

                if (candidate < 0 || (pos - candidate) > MaxDistance || candidate + MinMatch > source.Length)
                {
                    pos++;
                    continue;
                }

                if (Read32(source, candidate) != Read32(source, pos))
                {
                    pos++;
                    continue;
                }

                var matchPos = pos;
                var matchRef = candidate;
                if (preferRatio)
                {
                    while (matchPos > anchor && matchRef > 0 && source[matchPos - 1] == source[matchRef - 1])
                    {
                        matchPos--;
                        matchRef--;
                    }
                }

                var literalLength = matchPos - anchor;
                var matchLength = MinMatch;
                while (matchPos + matchLength < source.Length &&
                       source[matchRef + matchLength] == source[matchPos + matchLength])
                {
                    matchLength++;
                }

                EmitSequence(dst, source, anchor, literalLength, matchPos - matchRef, matchLength);

                var matchedUntil = matchPos + matchLength;
                var updateAt = matchPos + 1;
                while (updateAt + MinMatch <= matchedUntil && updateAt + MinMatch <= source.Length)
                {
                    hashTable[Hash32(Read32(source, updateAt))] = updateAt;
                    updateAt++;
                }

                pos = matchedUntil;
                anchor = pos;
            }

            var tailLength = source.Length - anchor;
            EmitLastLiterals(dst, source, anchor, tailLength);
            compressed = dst.ToArray();
            return true;
        }

        public static bool TryDecompress(byte[] compressed, int expectedLength, out byte[] decompressed)
        {
            decompressed = Array.Empty<byte>();
            if (compressed == null || expectedLength < 0)
            {
                return false;
            }
            if (expectedLength == 0)
            {
                decompressed = Array.Empty<byte>();
                return compressed.Length == 0;
            }

            var dst = new byte[expectedLength];
            var ip = 0;
            var op = 0;

            while (ip < compressed.Length)
            {
                var token = compressed[ip++];
                var literalLength = token >> 4;
                if (literalLength == 15)
                {
                    while (ip < compressed.Length && compressed[ip] == 255)
                    {
                        literalLength += 255;
                        ip++;
                    }
                    if (ip >= compressed.Length)
                    {
                        return false;
                    }
                    literalLength += compressed[ip++];
                }

                if (ip + literalLength > compressed.Length || op + literalLength > dst.Length)
                {
                    return false;
                }

                Buffer.BlockCopy(compressed, ip, dst, op, literalLength);
                ip += literalLength;
                op += literalLength;

                if (ip >= compressed.Length)
                {
                    break;
                }
                if (ip + 2 > compressed.Length)
                {
                    return false;
                }

                var offset = compressed[ip] | (compressed[ip + 1] << 8);
                ip += 2;
                if (offset <= 0 || offset > op)
                {
                    return false;
                }

                var matchLength = (token & 0x0F) + MinMatch;
                if ((token & 0x0F) == 15)
                {
                    while (ip < compressed.Length && compressed[ip] == 255)
                    {
                        matchLength += 255;
                        ip++;
                    }
                    if (ip >= compressed.Length)
                    {
                        return false;
                    }
                    matchLength += compressed[ip++];
                }

                if (op + matchLength > dst.Length)
                {
                    return false;
                }

                var copyFrom = op - offset;
                for (var i = 0; i < matchLength; i++)
                {
                    dst[op++] = dst[copyFrom + i];
                }
            }

            if (op != dst.Length)
            {
                return false;
            }

            decompressed = dst;
            return true;
        }

        private static void EmitSequence(
            List<byte> dst,
            byte[] source,
            int literalStart,
            int literalLength,
            int offset,
            int matchLength)
        {
            var tokenIndex = dst.Count;
            dst.Add(0);

            var token = 0;
            if (literalLength >= 15)
            {
                token |= (15 << 4);
                WriteLength(dst, literalLength - 15);
            }
            else
            {
                token |= (literalLength << 4);
            }

            for (var i = 0; i < literalLength; i++)
            {
                dst.Add(source[literalStart + i]);
            }

            dst.Add((byte)(offset & 0xFF));
            dst.Add((byte)((offset >> 8) & 0xFF));

            var encodedMatchLength = matchLength - MinMatch;
            if (encodedMatchLength >= 15)
            {
                token |= 15;
                WriteLength(dst, encodedMatchLength - 15);
            }
            else
            {
                token |= encodedMatchLength;
            }

            dst[tokenIndex] = (byte)token;
        }

        private static void EmitLastLiterals(List<byte> dst, byte[] source, int start, int length)
        {
            var token = 0;
            if (length >= 15)
            {
                token = (15 << 4);
                dst.Add((byte)token);
                WriteLength(dst, length - 15);
            }
            else
            {
                token = (length << 4);
                dst.Add((byte)token);
            }

            for (var i = 0; i < length; i++)
            {
                dst.Add(source[start + i]);
            }
        }

        private static void WriteLength(List<byte> dst, int value)
        {
            while (value >= 255)
            {
                dst.Add(255);
                value -= 255;
            }
            dst.Add((byte)value);
        }

        private static uint Read32(byte[] source, int offset)
        {
            return (uint)(source[offset] |
                         (source[offset + 1] << 8) |
                         (source[offset + 2] << 16) |
                         (source[offset + 3] << 24));
        }

        private static int Hash32(uint value)
        {
            return (int)((value * 2654435761U) >> (32 - HashBits));
        }
    }
}
