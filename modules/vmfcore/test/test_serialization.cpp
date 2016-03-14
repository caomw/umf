/* 
 * Copyright 2015 Intel(r) Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http ://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "test_precomp.hpp"
#include "fstream"

using namespace vmf;

enum SerializerType
{
    TypeXML = 0,
    TypeJson = 1
};

enum CryptAlgo
{
    DEFAULT, BAD, NONE
};

//Some testing class for encryption
class BadEncryptor : public vmf::Encryptor
{
public:
    BadEncryptor(char _key) : key(_key) { }

    virtual void encrypt(const vmf_string &input, vmf_rawbuffer &output)
    {
        output.clear();
        output.reserve(input.length());
        for(char c : input)
        {
            output.push_back(c ^ key);
        }
    }

    virtual void decrypt(const vmf_rawbuffer &input, vmf_string &output)
    {
        output.clear();
        output.reserve(input.size());
        for(char c : input)
        {
            output.push_back(c ^ key);
        }
    }

    virtual vmf_string getHint()
    {
        return vmf_string("bad encryptor for tests");
    }

    virtual ~BadEncryptor() { }

private:
    char key;
};


class TestSerialization : public ::testing::TestWithParam< std::tuple<SerializerType, vmf_string, CryptAlgo> >
{
protected:
    void SetUp()
    {
        n_schemaPeople = "people";
        n_schemaFrames = "frames";

        spSchemaFrames = std::make_shared<MetadataSchema>(n_schemaFrames);
        vFieldsFrames.push_back(FieldDesc("frameIdx", Variant::type_integer));
        vFieldsFrames.push_back(FieldDesc("score", Variant::type_real, true));
        vRefDescsFrames.emplace_back(std::make_shared<ReferenceDesc>("index1"));
        vRefDescsFrames.emplace_back(std::make_shared<ReferenceDesc>("index2"));
        spDescFrames = std::make_shared<MetadataDesc>("frame", vFieldsFrames, vRefDescsFrames);
        spSchemaFrames->add(spDescFrames);

        spSchemaPeople = std::make_shared<MetadataSchema>(n_schemaPeople);
        vFieldsPeople.push_back(FieldDesc("name", Variant::type_string));
        vFieldsPeople.push_back(FieldDesc("address", Variant::type_string, true));
        vRefDescsPeople.emplace_back(std::make_shared<ReferenceDesc>("friend"));
        vRefDescsPeople.emplace_back(std::make_shared<ReferenceDesc>("colleague"));
        spDescPeople = std::make_shared<MetadataDesc>("person", vFieldsPeople, vRefDescsPeople);
        spSchemaPeople->add(spDescPeople);

        stream.addSchema(spSchemaFrames);
        stream.addSchema(spSchemaPeople);

        std::shared_ptr<Metadata> person(new Metadata(spDescPeople));
        person->setFieldValue("name", "TestPersonName");
        person->setTimestamp(getTimestamp());
        set.push_back(person);
        stream.add(person);

        for(int i = 0; i < 10; i++)
        {
            std::shared_ptr<Metadata> md(new Metadata(spDescFrames));
            md->setFieldValue("frameIdx", i);
            md->setFrameIndex((long long)i << 33);
            set.push_back(md);
            stream.add(md);

            if(i%2 == 0)
            {
                md->addReference(person, (i%4? "index1" : ""));
                person->addReference(md, (i%4? "friend" : ""));
            }
        }

        segments.push_back(std::make_shared<MetadataStream::VideoSegment>("segment1", 30, 0, 1000, 800, 600));
        segments.push_back(std::make_shared<MetadataStream::VideoSegment>("segment2", 25, 5000, 1000));
    }

    void compareSchemas(const std::shared_ptr<MetadataSchema>& goldSchema, const std::shared_ptr<MetadataSchema>& testSchema, bool compareRefs = true)
    {
        ASSERT_EQ(goldSchema->getName(), testSchema->getName());
        ASSERT_EQ(goldSchema->getAuthor(), testSchema->getAuthor());

        auto goldDescs = goldSchema->getAll();
        auto testDescs = testSchema->getAll();
        ASSERT_EQ(goldDescs.size(), testDescs.size());

        for(unsigned int i = 0; i < testDescs.size(); i++)
        {
            ASSERT_EQ(goldDescs[i]->getMetadataName(), testDescs[i]->getMetadataName());

            auto goldFields = goldDescs[i]->getFields();
            auto testFileds = testDescs[i]->getFields();
            ASSERT_EQ(goldFields.size(), testFileds.size());

            for(unsigned int j = 0; j < testFileds.size(); j++)
            {
                ASSERT_EQ(goldFields[j].name, testFileds[j].name);
                ASSERT_EQ(goldFields[j].type, testFileds[j].type);
                ASSERT_EQ(goldFields[j].optional, testFileds[j].optional);
            }

            if (compareRefs)
            {
                auto goldRefDescs = goldDescs[i]->getAllReferenceDescs();
                auto testRefDescs = testDescs[i]->getAllReferenceDescs();
                ASSERT_EQ(goldRefDescs.size(), testRefDescs.size());

                for (unsigned int k = 0; k < testRefDescs.size(); k++)
                {
                    ASSERT_EQ(goldRefDescs[k]->name, testRefDescs[k]->name);
                    ASSERT_EQ(goldRefDescs[k]->isUnique, testRefDescs[k]->isUnique);
                    ASSERT_EQ(goldRefDescs[k]->isCustom, testRefDescs[k]->isCustom);
                }
            }
        }
    }

    void compareMetadata(const std::shared_ptr<Metadata>& goldMd, const std::shared_ptr<Metadata>& testMd, bool compareRefs = true)
    {
        ASSERT_EQ(goldMd->size(), testMd->size());
        ASSERT_EQ(goldMd->getSchemaName(), testMd->getSchemaName());
        ASSERT_EQ(goldMd->getName(), testMd->getName());
        ASSERT_EQ(goldMd->getFrameIndex(), testMd->getFrameIndex());
        ASSERT_EQ(goldMd->getNumOfFrames(), testMd->getNumOfFrames());
        ASSERT_EQ(goldMd->getTime(), testMd->getTime());
        ASSERT_EQ(goldMd->getDuration(), testMd->getDuration());
        ASSERT_EQ(goldMd->getId(), testMd->getId());
        ASSERT_EQ(goldMd->getFieldNames().size(), testMd->getFieldNames().size());

        auto fieldNames = goldMd->getFieldNames(), testNames = testMd->getFieldNames();
        for(auto goldName = fieldNames.begin(), testName = testNames.begin(); goldName != fieldNames.end() && testName !=  testNames.end(); goldName++, testName++)
        {
            ASSERT_EQ(*goldName, *testName);
            ASSERT_TRUE(goldMd->getFieldValue(*goldName) == testMd->getFieldValue(*testName));
        }
        if(compareRefs)
        {
            auto goldRefs = goldMd->getAllReferences(), testRefs = testMd->getAllReferences();
            ASSERT_EQ(goldRefs.size(), testRefs.size());

            std::sort(goldRefs.begin(), goldRefs.end(), [](Reference a, Reference b){return b.getReferenceMetadata().lock()->getId() < a.getReferenceMetadata().lock()->getId(); });
            std::sort(testRefs.begin(), testRefs.end(), [](Reference a, Reference b){return b.getReferenceMetadata().lock()->getId() < a.getReferenceMetadata().lock()->getId(); });

            for(unsigned int i = 0; i < testRefs.size(); i++)
            {
                ASSERT_EQ(goldRefs[i].getReferenceMetadata().lock()->getId(), testRefs[i].getReferenceMetadata().lock()->getId());
                ASSERT_EQ(goldRefs[i].getReferenceDescription()->name, testRefs[i].getReferenceDescription()->name);
            }
        }
    }

    std::shared_ptr<vmf::Encryptor> getEncryptor(CryptAlgo algo)
    {
        switch(algo)
        {
            case CryptAlgo::DEFAULT:
                return std::make_shared<DefaultEncryptor>("thereisnospoon");
            case CryptAlgo::BAD:
                return std::make_shared<BadEncryptor>(42);
            default:
                return nullptr;
        }
    }

    void makeReaderWriter(SerializerType type, vmf_string compressorId, CryptAlgo algo,
                          bool encryptAll = true, bool ignoreUnknownEncryptor = false)
    {
        encryptor = getEncryptor(algo);

        std::shared_ptr<IWriter> formatWriter, cWriter;
        std::shared_ptr<IReader> formatReader, cReader;
        if (type == TypeXML)
        {
            formatWriter = std::make_shared<XMLWriter>();
            formatReader = std::make_shared<XMLReader>();
        }
        else if (type == TypeJson)
        {
            formatWriter = std::make_shared<JSONWriter>();
            formatReader = std::make_shared<JSONReader>();
        }
        cWriter = std::make_shared<WriterCompressed>(formatWriter, compressorId);
        cReader = std::make_shared<ReaderCompressed>(formatReader);
        writer.reset(new WriterEncrypted(cWriter, encryptor, encryptAll));
        reader.reset(new ReaderEncrypted(cReader, encryptor, ignoreUnknownEncryptor));
    }

    void compareSegments(const std::shared_ptr<MetadataStream::VideoSegment>& idealSegment, const std::shared_ptr<MetadataStream::VideoSegment>& s)
    {
        ASSERT_EQ(idealSegment->getTitle(), s->getTitle());
        ASSERT_EQ(idealSegment->getFPS(), s->getFPS());
        ASSERT_EQ(idealSegment->getTime(), s->getTime());
        ASSERT_EQ(idealSegment->getDuration(), s->getDuration());
        long w1, h1, w2, h2;
        idealSegment->getResolution(w1, h1);
        s->getResolution(w2, h2);
        ASSERT_EQ(w1, w2);
        ASSERT_EQ(h1, h2);
    }

    MetadataStream stream;
    MetadataSet set;

    std::unique_ptr<IWriter> writer;
    std::unique_ptr<IReader> reader;
    std::shared_ptr<vmf::Encryptor> encryptor;
    std::shared_ptr< MetadataSchema > spSchemaPeople, spSchemaFrames;
    std::shared_ptr< MetadataDesc > spDescPeople, spDescFrames;
    std::vector< FieldDesc > vFieldsPeople, vFieldsFrames;
    std::vector<std::shared_ptr<ReferenceDesc>> vRefDescsPeople, vRefDescsFrames;
    std::vector< std::shared_ptr<MetadataStream::VideoSegment>> segments;

    vmf_string n_schemaPeople, n_schemaFrames;
};


TEST_P(TestSerialization, StoreAll)
{
    SerializerType type = std::get<0>(GetParam());
    
    if (type == TypeXML)
    {
        writer.reset(new XMLWriter());
        reader.reset(new XMLReader());
    }
    else if (type == TypeJson)
    {
        writer.reset(new JSONWriter());
        reader.reset(new JSONReader());
    }

    std::vector<std::shared_ptr<MetadataSchema>> schemas;

    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);

    std::shared_ptr<vmf::MetadataStream::VideoSegment> nullSegment = nullptr;
    segments.push_back(nullSegment);

    ASSERT_THROW(writer->store(1, "", stream.getChecksum(), segments, schemas, set, ""), vmf::IncorrectParamException);

    segments.pop_back();
    
    std::shared_ptr<Metadata> nullElement = nullptr;
    set.push_back(nullElement);

    ASSERT_THROW(writer->store(1, "", stream.getChecksum(), segments, schemas, set, ""), vmf::IncorrectParamException);

    set.pop_back();

    std::shared_ptr< MetadataSchema > spSchemaNull = nullptr;
    schemas.push_back(spSchemaNull);

    ASSERT_THROW(writer->store(1, "", stream.getChecksum(), segments, schemas, set, ""), vmf::IncorrectParamException);

    set.clear();

    ASSERT_THROW(writer->store(1, "", stream.getChecksum(), segments, schemas, set, ""), vmf::IncorrectParamException);

    auto spNewDesc = std::make_shared<vmf::MetadataDesc>("new", vFieldsPeople, vRefDescsFrames);
    schemas.pop_back();
    std::string check = "";
    std::shared_ptr<Metadata> md1(new Metadata(spNewDesc));
    std::shared_ptr<Metadata> md2(new Metadata(spNewDesc));
    set.push_back(md1);
    set.push_back(md2);

    ASSERT_THROW(writer->store(1, "", check, segments, schemas, set, ""), vmf::IncorrectParamException);

    segments.clear();
    schemas.clear();
    std::vector<std::shared_ptr<vmf::MetadataInternal>> mdInt;
    IdType nextId = 1;
    std::string path = "";
    
    std::string hint;
    ASSERT_FALSE(reader->parseAll("", nextId, path, check, hint, segments, schemas, mdInt));
}


TEST_P(TestSerialization, Parse_schemasArray)
{
    auto param = GetParam();
    makeReaderWriter(std::get<0>(param), std::get<1>(param), std::get<2>(param));

    std::vector<std::shared_ptr<MetadataSchema>> schemas;
    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);
    std::string result = writer->store(schemas);

    reader->parseSchemas(result, schemas);
    ASSERT_EQ(2u, schemas.size());
    std::for_each(schemas.begin(), schemas.end(), [&] (const std::shared_ptr<MetadataSchema>& spSchema)
    {
        compareSchemas(stream.getSchema(spSchema->getName()), spSchema);
    });

    schemas.clear();
    std::shared_ptr< MetadataSchema > spSchemaNull = nullptr;
    schemas.emplace_back(spSchemaNull);
    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);
    ASSERT_THROW(writer->store(schemas), vmf::IncorrectParamException);
}

TEST_P(TestSerialization, Parse_schemasAll)
{
    auto param = GetParam();
    makeReaderWriter(std::get<0>(param), std::get<1>(param), std::get<2>(param));

    std::vector<std::shared_ptr<MetadataSchema>> schemas;
    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);
    std::string result = stream.serialize(*writer);

    reader->parseSchemas(result, schemas);

    ASSERT_EQ(2u, schemas.size());
    compareSchemas(stream.getSchema(schemas[0]->getName()), schemas[0]);
    compareSchemas(stream.getSchema(schemas[1]->getName()), schemas[1]);
}


TEST_P(TestSerialization, Parse_metadataArray)
{
    SerializerType type = std::get<0>(GetParam());
    vmf_string compressorId = std::get<1>(GetParam());
    if (type == TypeXML)
    {
        writer.reset(new WriterCompressed(std::make_shared<XMLWriter>(), compressorId));
        reader.reset(new ReaderCompressed(std::make_shared<XMLReader>()));
    }
    else if (type == TypeJson)
    {
        writer.reset(new WriterCompressed(std::make_shared<JSONWriter>(), compressorId));
        reader.reset(new ReaderCompressed(std::make_shared<JSONReader>()));
    }

    std::string result = writer->store(set);

    std::vector<std::shared_ptr<MetadataSchema>> schemas;
    schemas.push_back(spSchemaFrames);
    schemas.push_back(spSchemaPeople);
    std::vector<std::shared_ptr<MetadataInternal>> md;
    reader->parseMetadata(result, schemas, md);
    ASSERT_EQ(set.size(), md.size());

    MetadataStream testStream;
    testStream.addSchema(spSchemaFrames);
    testStream.addSchema(spSchemaPeople);
    for(auto item = md.begin(); item != md.end(); item++)
        testStream.add(*item);

    ASSERT_EQ(set.size(), testStream.getAll().size());
    std::for_each(set.begin(), set.end(), [&] (const std::shared_ptr<Metadata>& spItem)
    {
        compareMetadata(spItem, testStream.getById(spItem->getId()) );
    });

    std::shared_ptr<Metadata> nullElement = nullptr;
    set.push_back(nullElement);
    ASSERT_THROW(writer->store(set), vmf::IncorrectParamException);
}

TEST_P(TestSerialization, Parse_metadataAll)
{
    auto param = GetParam();
    makeReaderWriter(std::get<0>(param), std::get<1>(param), std::get<2>(param));

    std::vector<std::shared_ptr<MetadataSchema>> schemas;
    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);
    std::string result = stream.serialize(*writer);

    std::vector<std::shared_ptr<MetadataInternal>> md;
    reader->parseMetadata(result, schemas, md);
    ASSERT_EQ(set.size(), md.size());

    MetadataStream testStream;
    testStream.addSchema(spSchemaFrames);
    testStream.addSchema(spSchemaPeople);
    for(auto item = md.begin(); item != md.end(); item++)
        testStream.add(*item);

    ASSERT_EQ(set.size(), testStream.getAll().size());
    std::for_each(set.begin(), set.end(), [&] (const std::shared_ptr<Metadata>& spItem)
    {
        compareMetadata(spItem, testStream.getById(spItem->getId()) );
    });
}

TEST_P(TestSerialization, Parse_All)
{
    auto param = GetParam();
    makeReaderWriter(std::get<0>(param), std::get<1>(param), std::get<2>(param));

    std::vector<std::shared_ptr<MetadataSchema>> schemas;
    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);
    std::string result = stream.serialize(*writer);

    MetadataStream testStream;
    testStream.deserialize(result, *reader);

    ASSERT_EQ(2u, testStream.getAllSchemaNames().size());
    compareSchemas(spSchemaPeople, testStream.getSchema(n_schemaPeople));
    compareSchemas(spSchemaFrames, testStream.getSchema(n_schemaFrames));

    ASSERT_EQ(set.size(), testStream.getAll().size());
    std::for_each(set.begin(), set.end(), [&] (const std::shared_ptr<Metadata>& spItem)
    {
        compareMetadata(spItem, testStream.getById(spItem->getId()) );
    });

    std::vector<std::shared_ptr<vmf::MetadataInternal>> mdInt;
    IdType nextId = 1;
    std::string path = "";
    std::string check = "";

    std::string hint;
    ASSERT_THROW(reader->parseAll("", nextId, path, check, hint, segments, schemas, mdInt), vmf::InternalErrorException);
}

TEST_P(TestSerialization, Parse_segmentArray)
{
    auto param = GetParam();
    makeReaderWriter(std::get<0>(param), std::get<1>(param), std::get<2>(param));

    std::string result = writer->store(segments);

    std::vector<std::shared_ptr<MetadataStream::VideoSegment>> loadedSegments;
    reader->parseVideoSegments(result, loadedSegments);

    ASSERT_EQ(segments.size(), loadedSegments.size());
    for (size_t i = 0; i < segments.size(); i++)
    {
        compareSegments(segments[i], loadedSegments[i]);
    }
}

TEST_P(TestSerialization, CheckIgnoreUnknownCompressor)
{
    auto param = GetParam();

    vmf_string compressorId = "unknown_compressor";
    std::shared_ptr<Compressor> fake = std::make_shared<FakeCompressor>();
    std::dynamic_pointer_cast<FakeCompressor>(fake)->setId(compressorId);
    vmf::Compressor::registerNew(fake);

    SerializerType type = std::get<0>(param);
    CryptAlgo algo      = std::get<2>(param);
    std::shared_ptr<vmf::Encryptor> encryptor = getEncryptor(algo);

    std::shared_ptr<IWriter> formatWriter, cWriter;
    std::shared_ptr<IReader> formatReader, cReader;
    if (type == TypeXML)
    {
        formatWriter = std::make_shared<XMLWriter>();
        formatReader = std::make_shared<XMLReader>();
    }
    else if (type == TypeJson)
    {
        formatWriter = std::make_shared<JSONWriter>();
        formatReader = std::make_shared<JSONReader>();
    }
    cWriter = std::make_shared<WriterCompressed>(formatWriter, compressorId);
    cReader = std::make_shared<ReaderCompressed>(formatReader, true);
    bool encryptAll = true;
    bool ignoreUnknownEncryptor = false;
    writer.reset(new WriterEncrypted(cWriter, encryptor, encryptAll));
    reader.reset(new ReaderEncrypted(cReader, encryptor, ignoreUnknownEncryptor));

    std::vector<std::shared_ptr<MetadataSchema>> schemas;
    schemas.push_back(spSchemaPeople);
    schemas.push_back(spSchemaFrames);
    std::string result = stream.serialize(*writer);

    vmf::Compressor::unregister(compressorId);

    reader->parseSchemas(result, schemas);

    ASSERT_EQ(1u, schemas.size());
    ASSERT_EQ("com.intel.vmf.compressed-metadata", schemas[0]->getName());
}


TEST_P(TestSerialization, CheckIgnoreUnknownEncryptor)
{
    auto param = GetParam();
    CryptAlgo algo = std::get<2>(param);
    if(algo != CryptAlgo::NONE)
    {
        makeReaderWriter(std::get<0>(param), std::get<1>(param), algo);

        std::vector<std::shared_ptr<MetadataSchema>> schemas;
        schemas.push_back(spSchemaPeople);
        schemas.push_back(spSchemaFrames);
        std::string result = stream.serialize(*writer);

        makeReaderWriter(std::get<0>(param), std::get<1>(param), CryptAlgo::NONE, true, true);

        reader->parseSchemas(result, schemas);
        ASSERT_EQ(schemas.size(), 1);
        ASSERT_EQ("com.intel.vmf.encrypted-metadata", schemas[0]->getName());
    }
}


TEST_P(TestSerialization, EncryptOneField)
{
    auto param = GetParam();
    CryptAlgo algo = std::get<2>(param);
    makeReaderWriter(std::get<0>(param), std::get<1>(param), algo, false);

    MetadataSet toEncSet = stream.queryBySchema(n_schemaPeople);
    ASSERT_EQ(toEncSet.size(), 1);
    std::shared_ptr<Metadata> toBeEncrypted  = toEncSet[0];

    if(algo != CryptAlgo::NONE)
    {
        toBeEncrypted->findField("name")->setUseEncryption(true);
        stream.setEncryptor(encryptor);
    }

    std::string result = stream.serialize(*writer);

    MetadataStream testStream;

    if(algo != CryptAlgo::NONE)
    {
        testStream.setEncryptor(encryptor);
    }

    testStream.deserialize(result, *reader);

    std::shared_ptr<Metadata> encrypted = testStream.getById(toBeEncrypted->getId());
    ASSERT_TRUE((bool)encrypted);
    compareMetadata(toBeEncrypted, encrypted);
}


TEST_P(TestSerialization, EncryptOneRecord)
{
    auto param = GetParam();
    CryptAlgo algo = std::get<2>(param);
    makeReaderWriter(std::get<0>(param), std::get<1>(param), algo, false);

    MetadataSet toEncSet = stream.queryBySchema(n_schemaPeople);
    ASSERT_EQ(toEncSet.size(), 1);
    std::shared_ptr<Metadata> toBeEncrypted  = toEncSet[0];
    if(algo != CryptAlgo::NONE)
    {
        stream.setEncryptor(encryptor);
        toBeEncrypted->setUseEncryption(true);
    }

    std::string result = stream.serialize(*writer);

    MetadataStream testStream;

    if(algo != CryptAlgo::NONE)
    {
        testStream.setEncryptor(encryptor);
    }

    testStream.deserialize(result, *reader);

    std::shared_ptr<Metadata> encrypted = testStream.getById(toBeEncrypted->getId());
    ASSERT_TRUE((bool)encrypted);
    compareMetadata(toBeEncrypted, encrypted);
}


TEST_P(TestSerialization, EncryptFieldDesc)
{
    auto param = GetParam();
    CryptAlgo algo = std::get<2>(param);
    makeReaderWriter(std::get<0>(param), std::get<1>(param), algo, false);

    std::shared_ptr< MetadataSchema > schema = stream.getSchema(n_schemaPeople);
    std::shared_ptr< MetadataDesc > metadesc = schema->findMetadataDesc("person");
    FieldDesc& field = metadesc->getFieldDesc("name");
    if(algo != CryptAlgo::NONE)
    {
        stream.setEncryptor(encryptor);
        field.useEncryption = true;
    }

    MetadataSet toEncSet = stream.queryBySchema(n_schemaPeople);
    ASSERT_EQ(toEncSet.size(), 1);
    std::shared_ptr<Metadata> toBeEncrypted  = toEncSet[0];

    std::string result = stream.serialize(*writer);

    MetadataStream testStream;

    if(algo != CryptAlgo::NONE)
    {
        testStream.setEncryptor(encryptor);
    }

    testStream.deserialize(result, *reader);

    schema = testStream.getSchema(n_schemaPeople);
    metadesc = schema->findMetadataDesc("person");
    field = metadesc->getFieldDesc("name");
    if(algo != CryptAlgo::NONE)
    {
        ASSERT_TRUE(field.useEncryption);
    }

    std::shared_ptr<Metadata> encrypted = testStream.getById(toBeEncrypted->getId());
    ASSERT_TRUE((bool)encrypted);
    compareMetadata(toBeEncrypted, encrypted);
}


TEST_P(TestSerialization, EncryptMetaDesc)
{
    auto param = GetParam();
    CryptAlgo algo = std::get<2>(param);
    makeReaderWriter(std::get<0>(param), std::get<1>(param), algo, false);

    std::shared_ptr< MetadataSchema > schema = stream.getSchema(n_schemaPeople);
    std::shared_ptr< MetadataDesc > metadesc = schema->findMetadataDesc("person");
    if(algo != CryptAlgo::NONE)
    {
        stream.setEncryptor(encryptor);
        metadesc->setUseEncryption(true);
    }

    MetadataSet toEncSet = stream.queryBySchema(n_schemaPeople);
    ASSERT_EQ(toEncSet.size(), 1);
    std::shared_ptr<Metadata> toBeEncrypted  = toEncSet[0];

    std::string result = stream.serialize(*writer);

    MetadataStream testStream;

    if(algo != CryptAlgo::NONE)
    {
        testStream.setEncryptor(encryptor);
    }

    testStream.deserialize(result, *reader);

    schema = testStream.getSchema(n_schemaPeople);
    metadesc = schema->findMetadataDesc("person");
    if(algo != CryptAlgo::NONE)
    {
        ASSERT_TRUE(metadesc->getUseEncryption());
    }

    std::shared_ptr<Metadata> encrypted = testStream.getById(toBeEncrypted->getId());
    ASSERT_TRUE((bool)encrypted);
    compareMetadata(toBeEncrypted, encrypted);
}


TEST_P(TestSerialization, EncryptSchema)
{
    auto param = GetParam();
    CryptAlgo algo = std::get<2>(param);
    makeReaderWriter(std::get<0>(param), std::get<1>(param), algo, false);

    std::shared_ptr< MetadataSchema > schema = stream.getSchema(n_schemaPeople);
    if(algo != CryptAlgo::NONE)
    {
        stream.setEncryptor(encryptor);
        schema->setUseEncryption(true);
    }

    MetadataSet toEncSet = stream.queryBySchema(n_schemaPeople);
    ASSERT_EQ(toEncSet.size(), 1);
    std::shared_ptr<Metadata> toBeEncrypted  = toEncSet[0];

    std::string result = stream.serialize(*writer);

    MetadataStream testStream;

    if(algo != CryptAlgo::NONE)
    {
        testStream.setEncryptor(encryptor);
    }

    testStream.deserialize(result, *reader);

    schema = testStream.getSchema(n_schemaPeople);
    if(algo != CryptAlgo::NONE)
    {
        ASSERT_TRUE(schema->getUseEncryption());
    }

    std::shared_ptr<Metadata> encrypted = testStream.getById(toBeEncrypted->getId());
    ASSERT_TRUE((bool)encrypted);
    compareMetadata(toBeEncrypted, encrypted);
}


//don't check for incorrect compressors
INSTANTIATE_TEST_CASE_P(UnitTest, TestSerialization,
                        ::testing::Combine(::testing::Values(TypeXML, TypeJson),
                                           ::testing::Values("com.intel.vmf.compressor.zlib", ""),
                                           ::testing::Values(CryptAlgo::DEFAULT,
                                                             CryptAlgo::BAD,
                                                             CryptAlgo::NONE)));
