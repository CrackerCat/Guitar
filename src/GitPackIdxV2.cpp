#include "GitPackIdxV2.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>

QString GitPackIdxV2::toString(uint8_t const *p)
{
	char tmp[41];
	for (int i = 0; i < 20; i++) {
		sprintf(tmp + i * 2, "%02x", p[i]);
	}
	return QString::fromLatin1(tmp, 40);
}

uint32_t GitPackIdxV2::read_uint32_be(const void *p)
{
	uint8_t const *q = (uint8_t const *)p;
	return (q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
}

uint32_t GitPackIdxV2::get_fanout(const GitPackIdxV2::header_t *t, int i)
{
	return read_uint32_be(&t->fanout[i]);
}

uint32_t GitPackIdxV2::count() const
{
	return get_fanout(&d.header, 255);
}

const uint8_t *GitPackIdxV2::object(int i) const
{
	return d.objects[i].id;
}

const uint32_t GitPackIdxV2::offset(int i) const
{
	return read_uint32_be(&d.offsets[i]);
}

const uint32_t GitPackIdxV2::checksum(int i) const
{
	return read_uint32_be(&d.checksums[i]);
}

bool GitPackIdxV2::parse(QIODevice *in)
{
	try {
		auto Read = [&](void *ptr, size_t len){
			return in->read((char *)ptr, len) == len;
		};
		static char const magic[] = "\xff\x74\x4f\x63\x00\x00\x00\x02";
		if (!Read(&d.header, sizeof(d.header)))    throw QString("failed to read the idx header");
		if (memcmp(d.header.magic, magic, 8) != 0) throw QString("invalid idx header");
		uint32_t size = count();
		if (size > 100000) throw QString("number of objects in the idx file is too big");
		size_t size4 = size * sizeof(uint32_t);
		d.objects.resize(size);
		d.checksums.resize(size);
		d.offsets.resize(size);
		if (!Read(&d.objects[0], size * 20))       throw QString("failed to read the objects");
		if (!Read(&d.checksums[0], size4))         throw QString("failed to read the checksums");
		if (!Read(&d.offsets[0], size4))           throw QString("failed to read the offsets");
		if (!Read(&d.trailer, sizeof(d.trailer)))  throw QString("failed to read the trailer");

		QCryptographicHash sha1(QCryptographicHash::Sha1);
		sha1.addData((char const *)&d.header, sizeof(d.header));
		sha1.addData((char const *)&d.objects[0], size * 20);
		sha1.addData((char const *)&d.checksums[0], size4);
		sha1.addData((char const *)&d.offsets[0], size4);
		sha1.addData((char const *)&d.trailer.packfile_checksum, sizeof(d.trailer) - 20);
		QByteArray chksum = sha1.result();
		Q_ASSERT(chksum.size() == 20);
		if (memcmp(chksum.data(), d.trailer.idxfile_checksum, 20) != 0) {
			throw QString("idx checksum is not correct");
		}

		for (size_t i = 0; i < size; i++) {
			Item item;
			item.id = toString(object(i));
			item.offset = offset(i);
			item.checksum = checksum(i);
			item_list.push_back(item);
		}
		std::sort(item_list.begin(), item_list.end(), [](Item const &left, Item const &right){
			return left.offset < right.offset;
		});
		for (size_t i = 0; i < size; i++) {
			Item &item = item_list[i];
			if (i + 1 < size) {
				item.packed_size = item_list[i + 1].offset - item_list[i].offset;
			}
			item_map[item.id] = item;
		}

		return true;
	} catch (QString const &e) {
		qDebug() << e;
	}
	return false;
}

void GitPackIdxV2::clear()
{
	d = Data();
}

bool GitPackIdxV2::parse(const QString &idxfile)
{
	clear();
	QFile file(idxfile);
	if (file.open(QFile::ReadOnly)) {
		if (parse(&file)) {
			return true;
		}
	}
	return false;
}

const GitPackIdxV2::Item *GitPackIdxV2::item(size_t i) const
{
	return &item_list[i];
}

const GitPackIdxV2::Item *GitPackIdxV2::item(const QString &id) const
{
	auto it = item_map.find(id);
	if (it == item_map.end()) {
		return nullptr;
	}
	return &it->second;
}

const std::map<QString, GitPackIdxV2::Item> *GitPackIdxV2::map() const
{
	return &item_map;
}
