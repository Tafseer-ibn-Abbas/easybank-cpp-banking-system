#include "sqlite3.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace std;
namespace fs = std::filesystem;

bool interactive = true, color = true;
const char *cyan = "\033[96m", *blue = "\033[94m", *gold = "\033[93m", *green = "\033[92m", *dim = "\033[90m", *red = "\033[91m", *reset = "\033[0m";
string paint(const string& s, const char* c) { return color ? string(c) + s + reset : s; }
void line() { cout << paint("  ----------------------------------------------------------------", dim) << '\n'; }
void screen(const string& heading, const string& subtitle = "") {
    if (interactive && color) cout << "\033[2J\033[H";
    cout << '\n' << paint("   E A S Y B A N K", cyan) << paint("    /    DIGITAL BANKING LAB", dim) << "\n\n";
    cout << "  " << paint(heading, blue) << "\n";
    if (!subtitle.empty()) cout << "  " << subtitle << '\n';
    line();
}
void message(const string& value, bool ok = true) { cout << "\n  " << paint(ok ? "[OK] " : "[!] ", ok ? green : gold) << value << "\n"; }
void pauseScreen() { if (interactive) { cout << "\n  Press Enter to continue..."; string x; getline(cin, x); } }
string trim(string s) {
    auto good = [](unsigned char c) { return !isspace(c); };
    s.erase(s.begin(), find_if(s.begin(), s.end(), good));
    s.erase(find_if(s.rbegin(), s.rend(), good).base(), s.end());
    return s;
}
string ask(const string& label) {
    cout << "  " << label << "  " << paint(">", cyan) << " ";
    string s;
    if (!getline(cin, s)) throw runtime_error("Input stream closed.");
    return trim(s);
}
string secret(const string& label) {
    cout << "  " << label << "  " << paint(">", cyan) << " ";
    string result;
#ifdef _WIN32
    if (interactive) {
        for (;;) {
            int ch = _getch();
            if (ch == 13) break;
            if (ch == 8 && !result.empty()) { result.pop_back(); cout << "\b \b"; }
            else if (ch == 0 || ch == 224) { _getch(); }
            else if (ch >= 32 && ch <= 126 && result.size() < 128) { result += char(ch); cout << '*'; }
        }
        cout << '\n';
        return result;
    }
#else
    if (interactive) {
        termios old{};
        if (tcgetattr(STDIN_FILENO, &old) == 0) {
            termios hidden = old;
            hidden.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
            if (!getline(cin, result)) result.clear();
            tcsetattr(STDIN_FILENO, TCSANOW, &old);
            cout << '\n';
            return result;
        }
    }
#endif
    if (!getline(cin, result)) throw runtime_error("Input stream closed.");
    return result;
}
int choice(int lo, int hi) {
    for (;;) {
        string s = ask("Choose");
        try { size_t n = 0; int i = stoi(s, &n); if (n == s.size() && i >= lo && i <= hi) return i; }
        catch (...) {}
        message("Select a valid menu number.", false);
    }
}
bool confirmed(const string& label) { string s = ask(label + " [type YES]"); return s == "YES"; }
string money(int64_t cents) {
    ostringstream s;
    if (cents < 0) { s << '-'; cents = -cents; }
    s << '$' << cents / 100 << '.' << setfill('0') << setw(2) << cents % 100;
    return s.str();
}
optional<int64_t> parseMoney(const string& text) {
    string s = trim(text);
    if (s.empty() || s.size() > 13) return {};
    auto dot = s.find('.');
    if (dot != string::npos && s.find('.', dot + 1) != string::npos) return {};
    string dollars = dot == string::npos ? s : s.substr(0, dot);
    string fractional = dot == string::npos ? "" : s.substr(dot + 1);
    auto numeric = [](string v) { return !v.empty() && all_of(v.begin(), v.end(), [](unsigned char c) {return isdigit(c);}); };
    if (!numeric(dollars) || fractional.size() > 2 || (dot != string::npos && !numeric(fractional))) return {};
    try {
        int64_t d = stoll(dollars);
        if (d > 10000000) return {};
        int64_t f = fractional.empty() ? 0 : stoll(fractional) * (fractional.size() == 1 ? 10 : 1);
        int64_t c = d * 100 + f;
        if (c > 1000000000) return {};
        return c;
    } catch (...) { return {}; }
}
int64_t amount() {
    for (;;) {
        auto val = parseMoney(ask("Amount USD (example 25.50)"));
        if (val && *val > 0) return *val;
        message("Enter a positive amount up to $10,000,000.00 (two decimals max).", false);
    }
}
vector<unsigned char> randomBytes(size_t n) {
    vector<unsigned char> bytes(n);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw runtime_error("Secure random generation failed.");
#else
    if (RAND_bytes(bytes.data(), (int)n) != 1) throw runtime_error("Secure random generation failed.");
#endif
    return bytes;
}
string hex(const vector<unsigned char>& b) {
    ostringstream out;
    for (auto c : b) out << std::hex << setfill('0') << setw(2) << (unsigned int)c;
    return out.str();
}
vector<unsigned char> unhex(const string& s) {
    if (s.size() % 2) throw runtime_error("Malformed password hash.");
    vector<unsigned char> out;
    for (size_t i = 0; i < s.size(); i += 2) {
        unsigned int n;
        istringstream stream(s.substr(i, 2)); stream >> std::hex >> n;
        if (stream.fail()) throw runtime_error("Malformed password hash.");
        out.push_back((unsigned char)n);
    }
    return out;
}
vector<unsigned char> derive(const string& password, const vector<unsigned char>& salt, int rounds) {
    vector<unsigned char> result(32);
#ifdef _WIN32
    BCRYPT_ALG_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) throw runtime_error("Password hashing unavailable.");
    auto status = BCryptDeriveKeyPBKDF2(h, (PUCHAR)password.data(), (ULONG)password.size(), (PUCHAR)salt.data(), (ULONG)salt.size(), (ULONGLONG)rounds, result.data(), (ULONG)result.size(), 0);
    BCryptCloseAlgorithmProvider(h, 0);
    if (status != 0) throw runtime_error("Password hashing failed.");
#else
    if (PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(), salt.data(), (int)salt.size(), rounds, EVP_sha256(), (int)result.size(), result.data()) != 1) throw runtime_error("Password hashing failed.");
#endif
    return result;
}
string hashPassword(const string& password) {
    auto salt = randomBytes(16);
    return "pbkdf2-sha256$120000$" + hex(salt) + '$' + hex(derive(password, salt, 120000));
}
bool matches(const string& password, const string& stored) {
    size_t a = stored.find('$'), b = stored.find('$', a + 1), c = stored.find('$', b + 1);
    if (a == string::npos || b == string::npos || c == string::npos || stored.substr(0, a) != "pbkdf2-sha256") return false;
    int rounds = 0;
    try { rounds = stoi(stored.substr(a + 1, b - a - 1)); } catch (...) { return false; }
    if (rounds < 10000 || rounds > 500000) return false;
    auto salt = unhex(stored.substr(b + 1, c - b - 1)), expected = unhex(stored.substr(c + 1));
    if (salt.size() != 16 || expected.size() != 32) return false;
    auto actual = derive(password, salt, rounds);
    unsigned char difference = 0;
    for (size_t i = 0; i < expected.size(); ++i) difference |= static_cast<unsigned char>(actual[i] ^ expected[i]);
    return difference == 0;
}
struct Statement {
    sqlite3_stmt* p = nullptr;
    Statement(sqlite3* db, const string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &p, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(p); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(int i, const string& s) { if (sqlite3_bind_text(p, i, s.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) throw runtime_error("Failed to bind text."); }
    void bind(int i, int64_t n) { if (sqlite3_bind_int64(p, i, n) != SQLITE_OK) throw runtime_error("Failed to bind number."); }
    int step() { return sqlite3_step(p); }
    int64_t number(int i) const { return sqlite3_column_int64(p, i); }
    string text(int i) const { auto ptext = sqlite3_column_text(p, i); return ptext ? (const char*)ptext : ""; }
};
struct Database {
    sqlite3* db = nullptr;
    explicit Database(const string& file) {
        if (sqlite3_open(file.c_str(), &db) != SQLITE_OK) { string e = db ? sqlite3_errmsg(db) : "open failure"; sqlite3_close(db); db = nullptr; throw runtime_error("Cannot open database: " + e); }
        sqlite3_busy_timeout(db, 4000);
        exec("PRAGMA foreign_keys=ON;");
        exec("CREATE TABLE IF NOT EXISTS accounts(account_no INTEGER PRIMARY KEY, username TEXT COLLATE NOCASE UNIQUE NOT NULL, password_hash TEXT NOT NULL, balance_cents INTEGER NOT NULL DEFAULT 0 CHECK(balance_cents>=0), status INTEGER NOT NULL DEFAULT 1 CHECK(status IN(0,1,2)), failed_attempts INTEGER NOT NULL DEFAULT 0, account_type TEXT NOT NULL CHECK(account_type IN('Savings','Checking')), last_interest_period TEXT NOT NULL);");
        exec("CREATE TABLE IF NOT EXISTS ledger(id INTEGER PRIMARY KEY AUTOINCREMENT, account_no INTEGER NOT NULL REFERENCES accounts(account_no), kind TEXT NOT NULL, amount_cents INTEGER NOT NULL, related_account INTEGER, created_at TEXT NOT NULL DEFAULT (datetime('now')));");
        exec("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
        exec("CREATE INDEX IF NOT EXISTS idx_ledger_account ON ledger(account_no,id DESC);");
    }
    ~Database() { sqlite3_close(db); }
    void exec(const string& query) {
        char* err = nullptr;
        int rc = sqlite3_exec(db, query.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) { string reason = err ? err : "SQLite failure"; sqlite3_free(err); throw runtime_error(reason); }
    }
    int64_t count(const string& sql) { Statement s(db, sql); return s.step() == SQLITE_ROW ? s.number(0) : 0; }
    string setting(const string& key) { Statement s(db, "SELECT value FROM settings WHERE key=?"); s.bind(1, key); return s.step() == SQLITE_ROW ? s.text(0) : ""; }
    void setSetting(const string& key, const string& value) { Statement s(db,"INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"); s.bind(1,key); s.bind(2,value); if (s.step()!=SQLITE_DONE) throw runtime_error(sqlite3_errmsg(db)); }
    void begin() { exec("BEGIN IMMEDIATE"); }
    void commit() { exec("COMMIT"); }
    void rollback() { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
};
struct Transaction {
    Database& d; bool done = false;
    explicit Transaction(Database& db) : d(db) { d.begin(); }
    ~Transaction() { if (!done) d.rollback(); }
    void commit() { d.commit(); done = true; }
};
void changed(Database& d, Statement& s) { if (s.step() != SQLITE_DONE || sqlite3_changes(d.db) != 1) throw runtime_error("Operation rejected; account may be inactive or funds insufficient."); }
void logEvent(Database& d, int acc, const string& type, int64_t amountCents, int related = 0) {
    Statement s(d.db, "INSERT INTO ledger(account_no,kind,amount_cents,related_account) VALUES(?,?,?,?)");
    s.bind(1, (int64_t)acc); s.bind(2, type); s.bind(3, amountCents);
    if (related) s.bind(4, (int64_t)related);
    if (s.step() != SQLITE_DONE) throw runtime_error(sqlite3_errmsg(d.db));
}
string periodNow() { time_t now = time(nullptr); tm date{};
#ifdef _WIN32
    localtime_s(&date, &now);
#else
    localtime_r(&now, &date);
#endif
    char buf[12]; strftime(buf, sizeof(buf), "%Y-%m", &date); return buf;
}
int registerUser(Database& d, const string& name, const string& password, const string& type) {
    if (name.size() < 3 || name.size() > 28 || !all_of(name.begin(), name.end(), [](unsigned char c) {return isalnum(c) || c == '_';})) throw runtime_error("Username: 3-28 letters, digits or underscores.");
    if (password.size() < 8) throw runtime_error("Password must be at least 8 characters.");
    if (type != "Savings" && type != "Checking") throw runtime_error("Invalid account type.");
    Transaction tx(d);
    int acc = (int)d.count("SELECT COALESCE(MAX(account_no),1000)+1 FROM accounts");
    Statement s(d.db,"INSERT INTO accounts(account_no,username,password_hash,account_type,last_interest_period) VALUES(?,?,?,?,?)");
    s.bind(1, (int64_t)acc); s.bind(2, name); s.bind(3, hashPassword(password)); s.bind(4, type); s.bind(5, periodNow());
    if (s.step() != SQLITE_DONE) throw runtime_error(sqlite3_errmsg(d.db));
    tx.commit(); return acc;
}
struct Account { int id = 0, status = 0, attempts = 0; int64_t balance = 0; string username, password, type, interest; };
optional<Account> getAccount(Database& d, const string& handle) {
    bool digits = !handle.empty() && all_of(handle.begin(),handle.end(),[](unsigned char c){return isdigit(c);});
    if (digits && handle.size() > 9) return {};
    Statement s(d.db, digits ? "SELECT account_no,username,password_hash,balance_cents,status,failed_attempts,account_type,last_interest_period FROM accounts WHERE account_no=?" : "SELECT account_no,username,password_hash,balance_cents,status,failed_attempts,account_type,last_interest_period FROM accounts WHERE username=?");
    if (digits) s.bind(1, (int64_t)stoll(handle)); else s.bind(1, handle);
    if (s.step() != SQLITE_ROW) return {};
    return Account{(int)s.number(0), (int)s.number(4), (int)s.number(5), s.number(3), s.text(1), s.text(2), s.text(6), s.text(7)};
}
bool authenticate(Database& d, const string& handle, const string& pass, int& id) {
    auto u = getAccount(d, handle);
    if (!u || u->status != 1 || u->attempts >= 3) return false;
    if (matches(pass,u->password)) {
        Statement s(d.db,"UPDATE accounts SET failed_attempts=0 WHERE account_no=?"); s.bind(1,(int64_t)u->id); changed(d,s);
        id = u->id; return true;
    }
    Statement s(d.db,"UPDATE accounts SET failed_attempts=failed_attempts+1 WHERE account_no=?"); s.bind(1,(int64_t)u->id); changed(d,s);
    return false;
}
void deposit(Database& d, int acc, int64_t cents) {
    if (cents <= 0 || cents > 1000000000) throw runtime_error("Invalid amount.");
    Transaction tx(d);
    Statement s(d.db,"UPDATE accounts SET balance_cents=balance_cents+? WHERE account_no=? AND status=1 AND balance_cents+?<=1000000000");
    s.bind(1,cents); s.bind(2,(int64_t)acc); s.bind(3,cents); changed(d,s);
    logEvent(d,acc,"Deposit",cents); tx.commit();
}
void withdraw(Database& d, int acc, int64_t cents) {
    if (cents <= 0 || cents > 1000000000) throw runtime_error("Invalid amount.");
    Transaction tx(d);
    Statement s(d.db,"UPDATE accounts SET balance_cents=balance_cents-? WHERE account_no=? AND status=1 AND balance_cents>=?");
    s.bind(1,cents); s.bind(2,(int64_t)acc); s.bind(3,cents); changed(d,s);
    logEvent(d,acc,"Withdrawal",-cents); tx.commit();
}
void transfer(Database& d, int from, int to, int64_t cents) {
    if (from == to) throw runtime_error("Cannot transfer to the same account.");
    if (cents <= 0 || cents > 1000000000) throw runtime_error("Invalid amount.");
    Transaction tx(d);
    Statement out(d.db,"UPDATE accounts SET balance_cents=balance_cents-? WHERE account_no=? AND status=1 AND balance_cents>=?");
    out.bind(1,cents); out.bind(2,(int64_t)from); out.bind(3,cents); changed(d,out);
    Statement in(d.db,"UPDATE accounts SET balance_cents=balance_cents+? WHERE account_no=? AND status=1 AND balance_cents+?<=1000000000");
    in.bind(1,cents); in.bind(2,(int64_t)to); in.bind(3,cents); changed(d,in);
    logEvent(d,from,"Transfer sent",-cents,to);
    logEvent(d,to,"Transfer received",cents,from);
    tx.commit();
}
void interest(Database& d, int acc) {
    Transaction tx(d);
    auto u = getAccount(d, to_string(acc));
    if (!u || u->status != 1 || u->type != "Savings" || u->interest >= periodNow() || u->balance < 100) throw runtime_error("No monthly interest is currently available for this account.");
    int64_t credit = (u->balance + 100) / 200;
    Statement s(d.db,"UPDATE accounts SET balance_cents=balance_cents+?,last_interest_period=? WHERE account_no=? AND status=1 AND balance_cents+?<=1000000000");
    s.bind(1,credit); s.bind(2,periodNow()); s.bind(3,(int64_t)acc); s.bind(4,credit); changed(d,s);
    logEvent(d,acc,"Savings interest (0.5%)",credit);
    tx.commit();
}
void statement(Database& d, int acc, int limit = 10) {
    screen("ACCOUNT ACTIVITY", "Latest entries in your personal ledger");
    Statement s(d.db,"SELECT created_at,kind,amount_cents,related_account FROM ledger WHERE account_no=? ORDER BY id DESC LIMIT ?");
    s.bind(1,(int64_t)acc); s.bind(2,(int64_t)limit);
    cout << "  " << left << setw(20) << "DATE (UTC)" << setw(23) << "ACTIVITY" << right << setw(14) << "AMOUNT" << "\n";
    line();
    int rows = 0;
    while (s.step() == SQLITE_ROW) {
        cout << "  " << left << setw(20) << s.text(0) << setw(23) << s.text(1) << right << setw(14) << money(s.number(2));
        if (s.number(3)) cout << "  #" << s.number(3);
        cout << '\n'; ++rows;
    }
    if (!rows) cout << "  No transactions yet.\n";
    pauseScreen();
}
void createAccountFlow(Database& d) {
    screen("CREATE ACCOUNT", "Fictional funds only - not a licensed banking service");
    string name = ask("Username (3-28 characters)");
    string pass = secret("Password (8+ characters)");
    if (pass != secret("Confirm password")) throw runtime_error("Passwords do not match.");
    cout << "  Account type: 1 Savings  |  2 Checking\n";
    int id = registerUser(d,name,pass,choice(1,2)==1 ? "Savings":"Checking");
    message("Account created. Your account number is #"+to_string(id));
    pauseScreen();
}
void profile(Database& d, int id) {
    auto a = getAccount(d,to_string(id));
    screen("ACCOUNT DETAILS");
    cout << "  Number    #" << a->id << "\n  Username  " << a->username << "\n  Type      " << a->type << "\n  Balance   " << money(a->balance) << "\n  Status    Active\n";
    pauseScreen();
}
void changePassword(Database& d, int id) {
    screen("ACCOUNT SECURITY");
    auto a = getAccount(d,to_string(id));
    if (!matches(secret("Current password"), a->password)) throw runtime_error("Current password incorrect.");
    string pass = secret("New password (8+ characters)");
    if (pass.size() < 8 || pass != secret("Confirm new password")) throw runtime_error("Password validation failed.");
    Statement s(d.db,"UPDATE accounts SET password_hash=? WHERE account_no=?");
    s.bind(1,hashPassword(pass)); s.bind(2,(int64_t)id); changed(d,s);
    message("Password updated."); pauseScreen();
}
void accountSession(Database& d, int id) {
    while (true) {
        auto a = getAccount(d,to_string(id));
        if (!a || a->status != 1) { message("Account access ended.",false); return; }
        screen("WELCOME BACK, " + a->username, "Account #"+to_string(id)+"  /  "+a->type);
        cout << "  " << paint("AVAILABLE BALANCE", dim) << "\n  " << paint(money(a->balance), green) << "\n\n";
        cout << "  1   Add funds                  5   Statement\n";
        cout << "  2   Withdraw                   6   Account details\n";
        cout << "  3   Transfer                   7   Change password\n";
        cout << "  4   Monthly savings interest   0   Sign out\n\n";
        int op = choice(0,7);
        if (op == 0) return;
        try {
            if (op == 1) { screen("DEPOSIT FUNDS"); auto cents = amount(); deposit(d,id,cents); message(money(cents)+" added successfully."); pauseScreen(); }
            if (op == 2) { screen("WITHDRAW FUNDS"); auto cents = amount(); if (confirmed("Withdraw "+money(cents)+"?")) { withdraw(d,id,cents); message("Withdrawal completed."); pauseScreen(); } }
            if (op == 3) {
                screen("SEND A TRANSFER"); string target = ask("Recipient username or account #");
                auto recipient = getAccount(d,target);
                if (!recipient || recipient->status != 1) throw runtime_error("Recipient is not an active account.");
                auto cents = amount();
                if (confirmed("Transfer "+money(cents)+" to "+recipient->username+" (#"+to_string(recipient->id)+")?")) { transfer(d,id,recipient->id,cents); message("Transfer completed and recorded in both ledgers."); pauseScreen(); }
            }
            if (op == 4) { interest(d,id); message("Eligible monthly interest credited."); pauseScreen(); }
            if (op == 5) statement(d,id,15);
            if (op == 6) profile(d,id);
            if (op == 7) changePassword(d,id);
        } catch (const exception& ex) { message(ex.what(),false); pauseScreen(); }
    }
}
void loginFlow(Database& d) {
    screen("ACCOUNT SIGN IN", "Three failed attempts lock the account until admin reset");
    string name = ask("Username or account #"), password = secret("Password");
    int id = 0;
    if (!authenticate(d,name,password,id)) { message("Login failed, account inactive, or access locked.",false); pauseScreen(); return; }
    accountSession(d,id);
}
void listAccounts(Database& d) {
    screen("ACCOUNT DIRECTORY", "Fictional accounts, local instance only");
    cout << "  " << left << setw(10) << "ACCOUNT" << setw(23) << "USERNAME" << setw(13) << "TYPE" << setw(12) << "STATUS" << right << setw(13) << "BALANCE" << '\n';
    line();
    Statement s(d.db,"SELECT account_no,username,account_type,status,balance_cents FROM accounts ORDER BY account_no");
    while (s.step() == SQLITE_ROW) {
        string status = s.number(3)==1 ? "Active" : s.number(3)==0 ? "Frozen" : "Closed";
        cout << "  #" << left << setw(9) << s.number(0) << setw(23) << s.text(1).substr(0,20) << setw(13) << s.text(2) << setw(12) << status << right << setw(13) << money(s.number(4)) << '\n';
    }
    pauseScreen();
}
void manageAccount(Database& d) {
    screen("ACCOUNT CONTROLS", "Freeze, restore, unlock or close accounts");
    string input = ask("Account number or username");
    auto a = getAccount(d,input);
    if (!a) throw runtime_error("Account not found.");
    cout << "\n  #" << a->id << "  " << a->username << "   /   " << money(a->balance) << "\n";
    cout << "  1 Freeze   2 Restore   3 Unlock login   4 Close zero-balance account   0 Cancel\n";
    int op = choice(0,4);
    if (!op) return;
    if (op==4 && (a->balance!=0 || a->status==2)) throw runtime_error("Only open zero-balance accounts can be closed.");
    if (op==4 && !confirmed("Close account permanently? Ledger retained")) return;
    string sql = op==1 ? "UPDATE accounts SET status=0 WHERE account_no=? AND status=1" : op==2 ? "UPDATE accounts SET status=1 WHERE account_no=? AND status=0" : op==3 ? "UPDATE accounts SET failed_attempts=0 WHERE account_no=? AND status!=2" : "UPDATE accounts SET status=2 WHERE account_no=? AND balance_cents=0 AND status!=2";
    Statement s(d.db,sql); s.bind(1,(int64_t)a->id); changed(d,s);
    message("Account controls updated. Existing ledger remains intact."); pauseScreen();
}
void seedDemo(Database& d) {
    if (d.count("SELECT COUNT(*) FROM accounts") != 0) throw runtime_error("Demo data can only be loaded into an empty database.");
    Transaction tx(d);
    vector<pair<string,string>> accounts{{"alex_demo","Savings"},{"sam_demo","Checking"},{"jordan_demo","Savings"}};
    for (int i=0; i<3; ++i) {
        int id = 1001+i;
        Statement s(d.db,"INSERT INTO accounts(account_no,username,password_hash,account_type,last_interest_period) VALUES(?,?,?,?,?)");
        s.bind(1,(int64_t)id); s.bind(2,accounts[i].first); s.bind(3,hashPassword("DemoPass123!")); s.bind(4,accounts[i].second); s.bind(5,periodNow());
        if (s.step()!=SQLITE_DONE) throw runtime_error(sqlite3_errmsg(d.db));
    }
    tx.commit();
    deposit(d,1001,150000); deposit(d,1002,87500); deposit(d,1003,210000);
    transfer(d,1001,1002,12500); withdraw(d,1003,2500);
    message("Created three fictional accounts. Password: DemoPass123!"); pauseScreen();
}
void adminSession(Database& d) {
    while (true) {
        screen("ADMINISTRATOR CONSOLE", "Local portfolio banking simulation - not real financial software");
        auto active = d.count("SELECT COUNT(*) FROM accounts WHERE status=1");
        auto frozen = d.count("SELECT COUNT(*) FROM accounts WHERE status=0");
        auto records = d.count("SELECT COUNT(*) FROM ledger");
        auto total = d.count("SELECT COALESCE(SUM(balance_cents),0) FROM accounts");
        cout << "  " << paint("ACTIVE",cyan) << "  " << active << "     " << paint("FROZEN",gold) << "  " << frozen << "     " << paint("LEDGER ENTRIES",cyan) << "  " << records << "\n";
        cout << "  Simulated account balances combined: " << paint(money(total),green) << "\n\n";
        cout << "  1   View account directory\n  2   Account controls\n  3   Load sample accounts (empty database only)\n  4   Change admin password\n  0   Exit administrator\n\n";
        int op = choice(0,4);
        if (!op) return;
        try {
            if (op==1) listAccounts(d);
            if (op==2) manageAccount(d);
            if (op==3) { if (confirmed("Populate three fictional accounts?")) seedDemo(d); }
            if (op==4) {
                screen("ADMIN PASSWORD");
                if (!matches(secret("Current admin password"),d.setting("admin_hash"))) throw runtime_error("Current password incorrect.");
                string p = secret("New password (8+ chars)");
                if (p.size()<8 || p!=secret("Confirm password")) throw runtime_error("Password confirmation failed.");
                d.setSetting("admin_hash",hashPassword(p)); message("Admin password updated."); pauseScreen();
            }
        } catch (const exception& ex) { message(ex.what(),false); pauseScreen(); }
    }
}
void adminLogin(Database& d) {
    screen("ADMINISTRATOR ACCESS");
    if (!matches(secret("Admin password"),d.setting("admin_hash"))) { message("Invalid password.",false); pauseScreen(); return; }
    adminSession(d);
}
void onboarding(Database& d) {
    if (!d.setting("admin_hash").empty()) return;
    screen("SETUP YOUR LOCAL LAB", "First run: choose an administrator password");
    cout << "  The portfolio app stores fictional balances only.\n";
    string first = secret("Create admin password (8+ chars)");
    if (first.size() < 8 || first != secret("Confirm admin password")) throw runtime_error("Initial admin password not saved. Run the application again.");
    d.setSetting("admin_hash",hashPassword(first));
    message("Local admin access configured."); pauseScreen();
}
fs::path dataPath() {
    const char* custom = getenv("EASYBANK_DATA_DIR");
    if (custom && *custom) return fs::path(custom) / "bank.db";
#ifdef _WIN32
    const char* home = getenv("LOCALAPPDATA");
    if (!home || !*home) throw runtime_error("LOCALAPPDATA is not configured.");
    return fs::path(home) / "EasyBankPortfolio" / "bank.db";
#else
    const char* home = getenv("HOME");
    return fs::path(home ? home : ".") / ".local" / "share" / "EasyBankPortfolio" / "bank.db";
#endif
}
void setupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    DWORD mode = 0; HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(h,&mode) || !SetConsoleMode(h,mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) color=false;
    DWORD inputMode=0; interactive=GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),&inputMode)!=0;
#else
    interactive=isatty(STDIN_FILENO); color=isatty(STDOUT_FILENO);
#endif
    if (!interactive) color=false;
}
void selfTest() {
    auto temp = fs::temp_directory_path() / ("easybank-selftest-" + hex(randomBytes(8)) + ".db");
    try {
        {
            Database d(temp.string());
            if (parseMoney("-1") || parseMoney("NaN") || parseMoney("1.234") || parseMoney("0") != optional<int64_t>(0) || parseMoney("12.30") != optional<int64_t>(1230)) throw runtime_error("Money validation test failed.");
            int a=registerUser(d,"alpha","DemoPass123!","Savings"), b=registerUser(d,"beta","DemoPass123!","Checking");
            int id = 0;
            if (!authenticate(d,"alpha","DemoPass123!",id) || id!=a || authenticate(d,"beta","bad",id)) throw runtime_error("Authentication test failed.");
            deposit(d,a,10000); withdraw(d,a,1250); transfer(d,a,b,3000);
            if (getAccount(d,"alpha")->balance != 5750 || getAccount(d,"beta")->balance != 3000 || d.count("SELECT COUNT(*) FROM ledger") != 4) throw runtime_error("Ledger/balance test failed.");
            try { transfer(d,a,b,1000000); throw runtime_error("Overdraft accepted."); } catch (const exception& e) { if (string(e.what())=="Overdraft accepted.") throw; }
            if (getAccount(d,"alpha")->balance != 5750 || getAccount(d,"beta")->balance != 3000 || d.count("SELECT COUNT(*) FROM ledger")!=4) throw runtime_error("Atomic rollback failed.");
            try { transfer(d,a,a,100); throw runtime_error("Self-transfer accepted."); } catch (const exception& e) { if (string(e.what())=="Self-transfer accepted.") throw; }
            {
                Statement old(d.db,"UPDATE accounts SET last_interest_period='2025-01' WHERE account_no=?");
                old.bind(1,(int64_t)a); changed(d,old);
                interest(d,a);
                if (getAccount(d,"alpha")->balance != 5779) throw runtime_error("Interest calculation failed.");
                try { interest(d,a); throw runtime_error("Duplicate interest allowed."); }
                catch (const exception& e) { if (string(e.what())=="Duplicate interest allowed.") throw; }
            }
            {
                Statement freeze(d.db,"UPDATE accounts SET status=0 WHERE account_no=?");
                freeze.bind(1,(int64_t)b); changed(d,freeze);
                try { transfer(d,a,b,100); throw runtime_error("Frozen recipient accepted transfer."); }
                catch (const exception& e) { if (string(e.what())=="Frozen recipient accepted transfer.") throw; }
                if (getAccount(d,"alpha")->balance != 5779 || getAccount(d,"beta")->balance != 3000) throw runtime_error("Freeze rollback failed.");
            }
            for (int n=0;n<3;n++) authenticate(d,"alpha","bad",id);
            if (authenticate(d,"alpha","DemoPass123!",id)) throw runtime_error("Login locking failed.");
            Statement unlock(d.db,"UPDATE accounts SET failed_attempts=0 WHERE account_no=?"); unlock.bind(1,(int64_t)a); changed(d,unlock);
            if (!authenticate(d,"alpha","DemoPass123!",id)) throw runtime_error("Login unlock failed.");
        }
        cout << "EASYBANK_SELFTEST=PASS\n";
    } catch (...) { fs::remove(temp); throw; }
    fs::remove(temp);
}
int main(int argc, char** argv) {
    setupConsole();
    try {
        if (argc>1 && string(argv[1])=="--self-test") { selfTest(); return 0; }
        fs::path file = dataPath(); fs::create_directories(file.parent_path());
        Database db(file.u8string()); onboarding(db);
        while (true) {
            screen("YOUR MONEY. YOUR SANDBOX.", "Clean workflows for a fictional, offline banking experience");
            cout << "  " << paint("01",cyan) << "   Open a demo account\n";
            cout << "  " << paint("02",cyan) << "   Sign in to an account\n";
            cout << "  " << paint("03",cyan) << "   Administrator workspace\n";
            cout << "  " << paint("00",gold) << "   Exit\n\n";
            int op = choice(0,3);
            if (op==0) break;
            try { if (op==1) createAccountFlow(db); if (op==2) loginFlow(db); if (op==3) adminLogin(db); }
            catch (const exception& e) { message(e.what(),false); pauseScreen(); }
        }
        cout << "\n  Session closed. Local records retained.\n";
        return 0;
    } catch (const exception& e) { cerr << "EASYBANK_ERROR=" << e.what() << '\n'; return 1; }
}
