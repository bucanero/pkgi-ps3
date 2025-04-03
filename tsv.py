import os
import socket
import urllib.parse

def get_nas_ip():
    nas_ip = input("Enter the IP address of your NAS: ").strip()
    try:
        socket.inet_aton(nas_ip)  # Validate IP format
        return nas_ip
    except socket.error:
        print("Invalid IP address format. Please try again.")
        return get_nas_ip()

def get_protocol():
    choice = input("Enter 1 for FTP or 2 for HTTP: ").strip()
    if choice == "1":
        return "ftp", 21  # Default FTP port
    elif choice == "2":
        return "http", 8000  # Default HTTP server port
    else:
        print("Invalid choice. Please enter 1 or 2.")
        return get_protocol()

def generate_tsv(nas_ip, protocol, port):
    tsv_file = "local_packages.tsv"
    
    with open(tsv_file, "w", encoding="utf-8") as f:
        # Write header
        f.write("Title ID\tRegion\tName\tPKG direct link\tRAP\tContent ID\tLast Modification Date\tDownload .RAP file\tFile Size\tSHA256\n")
        
        for file in os.listdir():
            if file.endswith(".pkg"):
                file_size = os.path.getsize(file)
                encoded_file = urllib.parse.quote(file)  # URL encode file name
                pkg_link = f"{protocol}://{nas_ip}:{port}/{encoded_file}"
                f.write(f"\t\t{file}\t{pkg_link}\t\t\t\t\t{file_size}\t\n")
    
    print(f"TSV file '{tsv_file}' generated successfully.")

if __name__ == "__main__":
    nas_ip = get_nas_ip()
    protocol, port = get_protocol()
    generate_tsv(nas_ip, protocol, port)
