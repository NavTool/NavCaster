#pragma once
#include <string>
#include <list>

#define HEXCOVERTCH(src, dest)         \
    if (src >= '0' && src <= '9')      \
    {                                  \
        dest = src - '0';              \
    }                                  \
    else if (src >= 'a' && src <= 'f') \
    {                                  \
        dest = src - 'a' + 10;         \
    }                                  \
    else if (src >= 'A' && src <= 'F') \
    {                                  \
        dest = src - 'A' + 10;         \
    }                                  \
    else                               \
    {                                  \
        break;                         \
    }

class CRegister
{
public:
    CRegister();

    std::string genMachineCode(std::string input);
    bool checkRegest(std::string register_info, std::string license_code);
    // 注册表操作
    std::string getEndTime() { return m_endTime; }
    enum
    {
        ECB = 0,
        CBC = 1,
        CFB = 2
    };

private:
    void Xor(char *buff, char const *chain)
    {
        if (false == m_bKeyInit)
            return;
        for (int i = 0; i < m_blockSize; i++)
            *(buff++) ^= *(chain++);
    }

    void AES128Encrypt(std::string strSrc, const char key[16], std::string &strDest);
    void AES128Decrypt(std::string pstrSrc, const char key[16], std::string &strDest);

    bool MakeKey(char const *key, char const *chain, int keylength, int blockSize);
    bool Encrypt(char const *in, char *result, size_t n, int iMode);
    bool Decrypt(char const *in, char *result, size_t n, int iMode);
    void sz2HexString(unsigned char *szInput, int inputLen, char *szDestBuf, int iType = 0);
    void HexString2sz(char *szInput, int inputLen, unsigned char *szDestBuf);
    bool EncryptBlock(char const *in, char *result);
    bool DecryptBlock(char const *in, char *result);

    void DefEncryptBlock(char const *in, char *result);
    void DefDecryptBlock(char const *in, char *result);
    bool SerialRegester(std::string register_info, std::string license_code);

private:
    enum
    {
        DEFAULT_BLOCK_SIZE = 16
    };
    enum
    {
        MAX_BLOCK_SIZE = 32,
        MAX_ROUNDS = 14,
        MAX_KC = 8,
        MAX_BC = 8
    };

    // Auxiliary Functions
    // Multiply two elements of GF(2^m)
    static int Mul(int a, int b)
    {
        return (a != 0 && b != 0) ? sm_alog[(sm_log[a & 0xFF] + sm_log[b & 0xFF]) % 255] : 0;
    }

    // Convenience method used in generating Transposition Boxes
    static int Mul4(int a, char b[])
    {
        if (a == 0)
            return 0;
        a = sm_log[a & 0xFF];
        int a0 = (b[0] != 0) ? sm_alog[(a + sm_log[b[0] & 0xFF]) % 255] & 0xFF : 0;
        int a1 = (b[1] != 0) ? sm_alog[(a + sm_log[b[1] & 0xFF]) % 255] & 0xFF : 0;
        int a2 = (b[2] != 0) ? sm_alog[(a + sm_log[b[2] & 0xFF]) % 255] & 0xFF : 0;
        int a3 = (b[3] != 0) ? sm_alog[(a + sm_log[b[3] & 0xFF]) % 255] & 0xFF : 0;
        return a0 << 24 | a1 << 16 | a2 << 8 | a3;
    }

private:
    static const int sm_alog[256];
    static const int sm_log[256];
    static const int8_t sm_S[256];
    static const int8_t sm_Si[256];
    static const int sm_T1[256];
    static const int sm_T2[256];
    static const int sm_T3[256];
    static const int sm_T4[256];
    static const int sm_T5[256];
    static const int sm_T6[256];
    static const int sm_T7[256];
    static const int sm_T8[256];
    static const int sm_U1[256];
    static const int sm_U2[256];
    static const int sm_U3[256];
    static const int sm_U4[256];
    static const int8_t sm_rcon[30];
    static const int sm_shifts[3][4][2];
    // Error Messages
    static char const *sm_szErrorMsg1;
    static char const *sm_szErrorMsg2;
    // Key Initialization Flag
    bool m_bKeyInit;
    // Encryption (m_Ke) round key
    int m_Ke[MAX_ROUNDS + 1][MAX_BC];
    // Decryption (m_Kd) round key
    int m_Kd[MAX_ROUNDS + 1][MAX_BC];
    // Key Length
    int m_keylength;
    // Block Size
    int m_blockSize;
    // Number of Rounds
    int m_iROUNDS;
    // Chain Block
    char m_chain0[MAX_BLOCK_SIZE];
    char m_chain[MAX_BLOCK_SIZE];
    // Auxiliary private use buffers
    int tk[MAX_KC];
    int a[MAX_BC];
    int t[MAX_BC];

public:
    int m_client_limit;
    int m_server_limit;
    std::string m_endTime;
};
