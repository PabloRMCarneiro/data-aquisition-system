#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>

#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32];
    std::time_t timestamp;
    double value;
};
#pragma pack(pop)

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

int main(int argc, char* argv[]) {
    // Verifica se o número de argumentos é correto
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_binary_file> <output_text_file>" << std::endl;
        return 1;
    }

    const std::string binaryFilename = argv[1];
    const std::string textFilename = argv[2];

    std::ifstream binFile(binaryFilename, std::ios::binary);
    if (!binFile) {
        std::cerr << "Cannot open binary file: " << binaryFilename << std::endl;
        return 1;
    }

    std::ofstream txtFile(textFilename);
    if (!txtFile) {
        std::cerr << "Cannot open text file: " << textFilename << std::endl;
        return 1;
    }

    LogRecord record;
    while (binFile.read(reinterpret_cast<char*>(&record), sizeof(LogRecord))) {
        txtFile << record.sensor_id << ", "
                << time_t_to_string(record.timestamp) << ", "
                << record.value << std::endl;
    }

    binFile.close();
    txtFile.close();

    std::cout << "Conversion from binary to text complete." << std::endl;

    return 0;
}
