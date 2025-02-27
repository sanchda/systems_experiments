import lz4.frame
import binascii
import struct
import os
import argparse
import sys

def analyze_lz4_file(filename):
    """Analyze an LZ4 compressed file using the lz4 library"""

    try:
        file_size = os.path.getsize(filename)
        print(f"File path: {filename}")
        print(f"File size: {file_size} bytes")

        with open(filename, 'rb') as f:
            # Use lz4.frame to analyze the header
            data = f.read()

            # Print the file size
            print(f"File size: {file_size} bytes ({file_size / (1024*1024):.2f} MB)")

            # First, check if this is a valid LZ4 frame
            print("\nValidating LZ4 frame format...")
            try:
                # Correct usage of get_frame_info - it accepts only one argument
                frame_info = lz4.frame.get_frame_info(data)
                print("O Valid LZ4 frame detected")

                # Display frame info from the library
                print("\nFrame Information (from lz4.frame):")
                print(f"  Block Size ID: {frame_info.get('block_size_id')}")
                print(f"  Block Mode: {'Linked' if frame_info.get('block_linked') else 'Independent'}")
                print(f"  Content Checksum: {'Present' if frame_info.get('content_checksum') else 'None'}")
                print(f"  Block Checksum: {'Present' if frame_info.get('block_checksum') else 'None'}")

                if frame_info.get('content_size') > 0:
                    content_size = frame_info.get('content_size')
                    print(f"  Content Size: {content_size} bytes ({content_size / (1024*1024):.2f} MB)")
                    if file_size > 0:
                        compression_ratio = content_size / file_size
                        print(f"  Overall Compression Ratio: {compression_ratio:.2f}x")
                else:
                    print("  Content Size: Unknown (not specified in frame)")

            except Exception as e:
                print(f"X Not a valid LZ4 frame: {e}")
                print("Attempting manual analysis anyway...")

            # Reset file pointer for manual analysis
            f.seek(0)

            # Read the magic number
            magic = f.read(4)
            print(f"\nMagic Number: {binascii.hexlify(magic).decode()}")

            # Try to decompress the file and analyze chunks
            try:
                f.seek(0)
                decompressed_size = 0
                chunk_sizes = []

                # Check for the magic number
                print("\nAnalyzing chunks...")
                if magic == b'\x04\x22\x4d\x18':
                    # Standard LZ4 frame format, try to determine header size
                    # Minimum header size is 7 bytes (magic + FLG + BD + HC)
                    header_size = 7

                    # Read FLG byte to check for additional fields
                    f.seek(4)  # Skip magic
                    flg_byte = f.read(1)
                    if flg_byte:
                        flg = ord(flg_byte)
                        content_size_flag = (flg >> 3) & 0x1
                        dict_id_flag = (flg >> 0) & 0x1

                        # Add content size (8 bytes) if present
                        if content_size_flag:
                            header_size += 8

                        # Add dictionary ID (4 bytes) if present
                        if dict_id_flag:
                            header_size += 4

                    offset = header_size
                else:
                    # Unknown format, start after magic number as fallback
                    print("  Non-standard LZ4 format detected, analysis may be incomplete")
                    offset = 4

                # Read and analyze each chunk
                chunk_index = 0
                while offset < file_size:
                    f.seek(offset)

                    # Read chunk size (4 bytes)
                    chunk_size_data = f.read(4)
                    if not chunk_size_data or len(chunk_size_data) < 4:
                        print(f"  Reached end of file at offset {offset}")
                        break

                    chunk_size = struct.unpack('<I', chunk_size_data)[0]
                    is_uncompressed = bool(chunk_size & 0x80000000)
                    chunk_size = chunk_size & 0x7FFFFFFF

                    # Check for end mark
                    if chunk_size == 0:
                        print(f"  Chunk {chunk_index}: End mark at offset {offset}")
                        offset += 4
                        break

                    print(f"  Chunk {chunk_index}:")
                    print(f"    Offset: {offset} (0x{offset:X})")
                    print(f"    Size: {chunk_size} bytes")
                    print(f"    Type: {'Uncompressed' if is_uncompressed else 'Compressed'}")

                    # Read actual chunk data
                    f.seek(offset + 4)
                    chunk_data = f.read(chunk_size)

                    # Show data sample
                    if chunk_size > 0:
                        sample_size = min(16, chunk_size)
                        data_sample = binascii.hexlify(chunk_data[:sample_size]).decode()
                        print(f"    Data sample: {data_sample}{' (truncated)' if chunk_size > sample_size else ''}")

                    # Try to estimate uncompressed size
                    try:
                        if 'frame_info' in locals() and frame_info.get('block_size_id') is not None:
                            block_size_map = {
                                4: 64 * 1024,      # 64 KB
                                5: 256 * 1024,     # 256 KB
                                6: 1 * 1024 * 1024,  # 1 MB
                                7: 4 * 1024 * 1024   # 4 MB
                            }
                            max_chunk_size = block_size_map.get(frame_info.get('block_size_id'), 0)
                            if max_chunk_size > 0 and not is_uncompressed:
                                print(f"    Max uncompressed size: {max_chunk_size} bytes")
                                if chunk_size > 0:
                                    est_ratio = max_chunk_size / chunk_size
                                    print(f"    Est. compression ratio: up to {est_ratio:.2f}x")
                    except Exception:
                        pass

                    # Update offset for next chunk
                    offset += 4 + chunk_size

                    # Account for block checksum if present
                    if 'frame_info' in locals() and frame_info.get('block_checksum'):
                        if offset + 4 <= file_size:
                            checksum_bytes = f.read(4)
                            if len(checksum_bytes) == 4:
                                checksum = struct.unpack('<I', checksum_bytes)[0]
                                print(f"    Block checksum: 0x{checksum:08X}")
                            offset += 4

                    chunk_index += 1
                    chunk_sizes.append(chunk_size)

                # Print chunk statistics
                if chunk_sizes:
                    total_chunk_size = sum(chunk_sizes)
                    avg_chunk_size = total_chunk_size / len(chunk_sizes)
                    max_chunk_size = max(chunk_sizes)
                    min_chunk_size = min(chunk_sizes)

                    print("\nChunk Statistics:")
                    print(f"  Total chunks: {len(chunk_sizes)}")
                    print(f"  Total data size: {total_chunk_size} bytes")
                    print(f"  Average chunk size: {avg_chunk_size:.2f} bytes")
                    print(f"  Largest chunk: {max_chunk_size} bytes")
                    print(f"  Smallest chunk: {min_chunk_size} bytes")

                # Try to decompress the entire file to verify and get actual size
                try:
                    f.seek(0)
                    decompressed = lz4.frame.decompress(data)
                    print(f"\nSuccessfully decompressed entire file:")
                    print(f"  Actual uncompressed size: {len(decompressed)} bytes ({len(decompressed) / (1024*1024):.2f} MB)")
                    print(f"  Actual compression ratio: {len(decompressed) / file_size:.2f}x")
                except Exception as e:
                    print(f"\nCould not decompress entire file: {e}")

            except Exception as e:
                print(f"Error analyzing chunks: {e}")

    except FileNotFoundError:
        print(f"Error: File '{filename}' not found")
    except PermissionError:
        print(f"Error: Permission denied accessing file '{filename}'")
    except Exception as e:
        print(f"Error: {e}")

def main():
    parser = argparse.ArgumentParser(description='LZ4 file diagnostic tool using lz4 library')
    parser.add_argument('filename', help='Path to the LZ4 file to analyze')
    args = parser.parse_args()

    analyze_lz4_file(args.filename)

if __name__ == "__main__":
    main()
