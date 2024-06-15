#include "CommitMessageGenerator.h"
#include "ApplicationGlobal.h"
#include "ApplicationSettings.h"
#include "common/jstream.h"
#include "common/misc.h"
#include "common/strformat.h"
#include "webclient.h"
#include <QFile>
#include <QString>

namespace {

std::string encode_json_string(std::string const &in)
{
	std::string out;
	char const *ptr = in.c_str();
	char const *end = ptr + in.size();
	while (ptr < end) {
		char c = *ptr++;
		if (c == '"') {
			out += "\\\"";
		} else if (c == '\\') {
			out += "\\\\";
		} else if (c == '\b') {
			out += "\\b";
		} else if (c == '\f') {
			out += "\\f";
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\r') {
			out += "\\r";
		} else if (c == '\t') {
			out += "\\t";
		} else if (c < 32) {
			char tmp[10];
			sprintf(tmp, "\\u%04x", c);
			out += tmp;
		} else {
			out += c;
		}
	}
	return out;
}

std::string decode_json_string(std::string const &in)
{
	QString out;
	char const *ptr = in.c_str();
	char const *end = ptr + in.size();
	while (ptr < end) {
		char c = *ptr++;
		if (c == '\\') {
			if (ptr < end) {
				char d = *ptr++;
				if (d == '"') {
					out += '"';
				} else if (d == '\\') {
					out += '\\';
				} else if (d == '/') {
					out += '/';
				} else if (d == 'b') {
					out += '\b';
				} else if (d == 'f') {
					out += '\f';
				} else if (d == 'n') {
					out += '\n';
				} else if (d == 'r') {
					out += '\r';
				} else if (d == 't') {
					out += '\t';
				} else if (d == 'u') {
					if (ptr + 4 <= end) {
						char tmp[5];
						memcpy(tmp, ptr, 4);
						tmp[4] = 0;
						ushort c = strtol(tmp, nullptr, 16);
						out += QChar(c);
						ptr += 4;
					}
				}
			}
		} else {
			out += c;
		}
	}
	return out.toStdString();
}

} // namespace

std::vector<std::string> CommitMessageGenerator::parse_openai_response(std::string const &in)
{
	error_.clear();
	std::vector<std::string> lines;
	bool ok1 = false;
	std::string text;
	char const *begin = in.c_str();
	char const *end = begin + in.size();
	jstream::Reader r(begin, end);
	while (r.next()) {
		if (r.match("{object")) {
			if (r.string() == "chat.completion") {
				ok1 = true;
			}
		} else if (r.match("{choices[{message{content")) {
			text = decode_json_string(r.string());
		} else if (r.match("{error{type")) {
			error_ = r.string();
			return {};
		}
	}
	if (ok1) {
		misc::splitLines(text, &lines, false);
		size_t i = lines.size();
		while (i > 0) {
			i--;
			std::string_view sv = lines[i];
			char const *ptr = sv.data();
			char const *end = ptr + sv.size();
			while (ptr < end && *ptr == '`') ptr++;
			while (ptr < end && end[-1] == '`') end--;
			if (ptr < end && *ptr == '-') {
				ptr++;
			} else {
				while (ptr < end && isdigit((unsigned char)*ptr)) {
					ptr++;
				}
				if (ptr < end && *ptr == '.') {
					ptr++;
				}
			}
			while (ptr < end && isspace((unsigned char)*ptr)) {
				ptr++;
			}
			if (ptr + 1 < end && *ptr == '\"' && end[-1] == '\"') {
				ptr++;
				end--;
			}
			if (ptr < end) {
				lines[i] = std::string(ptr, end);
			} else {
				lines.erase(lines.begin() + i);
			}
		}
		return lines;
	}
	return {};
}

QStringList CommitMessageGenerator::generate(GitPtr g)
{
	QString diff = g->diff_head();
	if (diff.isEmpty()) return {};

	if (diff.size() > 100000) {
		qDebug() << "diff too large";
		return {};
	}

	std::string model = global->appsettings.openai_gpt_model.toStdString();
	if (model.empty()) model = "gpt-4o";

	constexpr int max = 5;

	// Referring to https://github.com/Nutlope/aicommits
	std::string prompt = strformat(
						  "Generate a concise git commit message written in present tense for the following code diff with the given specifications below. "
						  "Exclude anything unnecessary such as translation. "
						  "Your entire response will be passed directly into git commit. "
						  "Please generate %d messages, bulleted, and start writing with '-'. ")(max);
			;
	prompt = prompt + "\n\n" + diff.toStdString();

	std::string json = R"---({
	"model": "%s",
	"messages": [
		{"role": "system", "content": "You are a experienced engineer."},
		{"role": "user", "content": "%s"}]})---";
	json = strformat(json)(model)(encode_json_string(prompt));

	if (0) {
		QFile file("c:/a/a.txt");
		if (file.open(QIODevice::WriteOnly)) {
			file.write(json.c_str(), json.size());
		}
	}

	std::string url = "https://api.openai.com/v1/chat/completions";
	WebClient::Request rq(url);
	rq.add_header("Authorization: Bearer " + global->OpenAiApiKey().toStdString());
	WebClient::Post post;
	post.content_type = "application/json";
	post.data.insert(post.data.end(), json.begin(), json.end());

	WebClient http(&global->webcx);
	if (http.post(rq, &post)) {
		char const *data = http.content_data();
		size_t size = http.content_length();
		if (0) {
			QFile file("c:/a/b.txt");
			if (file.open(QIODevice::WriteOnly)) {
				file.write(data, size);
			}
		}
		std::string text(data, size);
		auto list = parse_openai_response(text);
		QStringList out;
		for (int i = 0; i < max && i < list.size(); i++) {
			out.push_back(QString::fromStdString(list[i]));
		}
		return out;
	}

	if (0) { // response example

		std::string example = R"---({
	  "id": "chatcmpl-9Q9tzFdQIw3NYSpwbgyFrG8EOJw29",
	  "object": "chat.completion",
	  "created": 1716021619,
	  "model": "gpt-4-0613",
	  "choices": [
		{
		  "index": 0,
		  "message": {
			"role": "assistant",
			"content": "1. \"Upgrade C++ version from C++11 to C++17 in strformat.pro\"\n2. \"Update strformat.pro to use C++17 instead of C++11\"\n3. \"Change C++ version in CONFIG from C++11 to C++17 in strformat.pro\""
		  },
		  "logprobs": null,
		  "finish_reason": "stop"
		}
	  ],
	  "usage": {
		"prompt_tokens": 145,
		"completion_tokens": 60,
		"total_tokens": 205
	  },
	  "system_fingerprint": null
	}
)---";
		{
			std::vector<std::string> lines = parse_openai_response(example);

			for (std::string const &line : lines) {
				fprintf(stderr, "%s\n", line.c_str());

			}
		}
	}

	return {};
}

