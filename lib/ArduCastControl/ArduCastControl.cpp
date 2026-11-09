#include "ArduCastControl.h"

#include "cast_channel.pb.h"
#include "authority_keys.pb.h"
#include "logging.pb.h"
 
#include "pb_common.h"
#include "pb.h"
#include "pb_encode.h"

#include "string.h"

const char CC_SOURCEID[] = "sender-0";
const char CC_MAIN_DESTIID[] = "receiver-0";
const char CC_NS_CONNECTION[] = "urn:x-cast:com.google.cast.tp.connection";
const char CC_NS_RECEIVER[] = "urn:x-cast:com.google.cast.receiver";
const char CC_NS_HEARTBEAT[] = "urn:x-cast:com.google.cast.tp.heartbeat";
const char CC_NS_MEDIA[] = "urn:x-cast:com.google.cast.media";
const char CC_MSG_CONNECT[] = "{\"type\": \"CONNECT\"}";
const char CC_MSG_PING[] = "{\"type\": \"PING\"}";
const char CC_MSG_GET_STATUS[] = "{\"type\": \"GET_STATUS\", \"requestId\": 1}"; 

//incomplet msgs as these need some args to the end
const char CC_MSG_PLAY[] = "{\"type\": \"PLAY\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_PAUSE[] = "{\"type\": \"PAUSE\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_NEXT[] = "{\"type\": \"QUEUE_NEXT\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_PREV[] = "{\"type\": \"QUEUE_PREV\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_SET_VOL[] = "{\"type\": \"SET_VOLUME\", \"requestId\": 2, \"volume\": {\"level\": ";//this need double braces!
const char CC_MSG_VOL_MUTE[] = "{\"type\": \"SET_VOLUME\", \"requestId\": 2, \"volume\": {\"muted\": ";//this need double braces!
static char cc_msg_ctrl[128];

// void ArduCastConnection::init(WiFiClientSecure& client, int keepAlive, uint8_t *writeBuffer, int writeBufferSize){
//   //this->client = client;
//   this->keepAlive = keepAlive;
//   this->writeBuffer = writeBuffer;
//   this->writeBufferSize = writeBufferSize;
//   connected = false;
// }


int ArduCastConnection::connect(const char* destinationId){
  // Serial.printf("Connect to %s\n", destinationId);
  strncpy(destId, destinationId, sizeof(destId));
  int err =  writeMsg(CC_NS_CONNECTION, CC_MSG_CONNECT);
  pinged(); //do not ping immediately - we probably want to check status anyway
  connected = true;
  return err;
}

void ArduCastConnection::pinged(){
  lastMsgAt = millis();
}

void ArduCastConnection::setDisconnect(){
  connected = false;
}

channelConnection_t ArduCastConnection::getConnectionStatus(){
  if ( !client.connected() || !connected )
    return CH_DISCONNECTED;
  if ( (unsigned long)(lastMsgAt + 3*keepAlive) < millis() ){
    connected = false;
    return CH_DISCONNECTED;
  }
  if ( (unsigned long)(lastMsgAt + keepAlive) < millis())
    return CH_NEEDS_PING;
  else
    return CH_CONNECTED;
}

const char* ArduCastConnection::getDestinationId(){
  return destId;
}

bool ArduCastConnection::encode_string(pb_ostream_t *stream, const pb_field_iter_t *field, void * const *arg)
{
  const char *str = (const char*)(*arg);

  // Serial.printf("Encode: %s (%d)\n",str, strlen(str));
  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

int ArduCastConnection::writeMsg(const char* nameSpace, const char* payload){
  if ( !client.connected() )
    return -1;
   
  extensions_api_cast_channel_CastMessage newMsg = extensions_api_cast_channel_CastMessage_init_zero;
  pb_ostream_t stream = pb_ostream_from_buffer(writeBuffer+4, writeBufferSize-4);
  newMsg.source_id.arg = (void*)CC_SOURCEID;
  newMsg.source_id.funcs.encode = encode_string;
  newMsg.destination_id.arg = (void*)destId;
  newMsg.destination_id.funcs.encode = encode_string;
  newMsg.namespace_fix.arg = (void*)nameSpace;
  newMsg.namespace_fix.funcs.encode = encode_string;
  newMsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
  newMsg.payload_utf8.arg = (void*)payload;
  newMsg.payload_utf8.funcs.encode = encode_string;

  bool status = pb_encode(&stream, extensions_api_cast_channel_CastMessage_fields, &newMsg);
  if (!status)
    return -2;
  
  writeBuffer[0] = (stream.bytes_written>>24) & 0xFF;
  writeBuffer[1] = (stream.bytes_written>>16) & 0xFF;
  writeBuffer[2] = (stream.bytes_written>>8) & 0xFF;
  writeBuffer[3] = (stream.bytes_written>>0) & 0xFF;

  uint32_t len = client.write(writeBuffer, stream.bytes_written+4);
  if (len < stream.bytes_written+4)
    return -3;
  
  return 0;
}


////////////////////////


uint32_t ArduCastControl::getIncomingMessageLength(WiFiClientSecure &client){
  // Unused on ESP32 path — length is read destructively in getRawMessage.
  (void)client;
  return 0;
}


uint32_t ArduCastControl::getRawMessage(uint8_t *buffer, uint16_t bufSize, WiFiClientSecure &client, uint32_t timeout){
  unsigned long start = millis();
  // Serial.printf("Checking read %d\n", client.available());
  if (bufSize <= 4 || client.available() < 4 ){
    return 0;
  }

  // ESP32 WiFiClientSecure lacks peekBytes(); consume the 4-byte length header.
  uint8_t lenbuf[4];
  if (client.readBytes(lenbuf, 4) != 4) {
    return 0;
  }
  uint16_t len = (lenbuf[0]<<24) + (lenbuf[1]<<16) + (lenbuf[2]<<8) + lenbuf[3];
  while ( client.available() < len ){
    if (millis() - start > timeout ){
      // Serial.println("timeout");
      while(client.available())
        client.read();
      return 0;
    }
  }

  // Reconstruct length prefix in output buffer.
  if (bufSize >= 4) {
    memcpy(buffer, lenbuf, 4);
  }

  uint32_t dump = 0;
  uint16_t body = len;
  if ( bufSize < (uint32_t)body + 4 ){
    dump = body - (bufSize-4);
    body = bufSize-4;
  }
  client.readBytes(buffer + 4, body);
  while ( dump > 0){
    client.read();
    dump--;
  }
  return body+4;
}



int ArduCastControl::connect(const char* host){
  client.setInsecure(); // chromecast uses a self-signed cert

  int err = client.connect(host, 8009);
  if ( !err ){
    return -10;
  }
  
  connectionStatus =  TCPALIVE;
  
  // deviceConnection.init(client, PING_TIMEOUT, connBuffer, CONNBUFFER_SIZE);
  // applicationConnection.init(client, PING_TIMEOUT, connBuffer, CONNBUFFER_SIZE);
  err = deviceConnection.connect(CC_MAIN_DESTIID);
  if ( err == 0 )
    connectionStatus = CONNECTED;
  return err;
}

void ArduCastControl::printRawMsg(int64_t len, uint8_t *buffer){

  Serial.printf("Message Length: %lld\n", len);
 
  Serial.print("Message: ");

  //python style print for compare
  for(int64_t i = 0; i<len; i++){
    if ( buffer[i] < 127 && buffer[i] > 31){
      Serial.printf("%c",buffer[i]);  
    } else {
      Serial.printf("\\x%02X",buffer[i]);
    }
  }
  //hex stream print for https://protogen.marcgravell.com/decode
  // for(int64_t i = 0; i<len; i++){
  //   Serial.printf("%02X",buffer[i]);
  // }
  Serial.println();
}

uint8_t ArduCastControl::pbDecodeVarint(uint8_t *bufferStart, uint32_t *decodedInt){
  *decodedInt = 0;
  int8_t decoded = -1;
  do {
    decoded++;
    //Serial.printf("curr=0x%02x, in=0x%02x, d=%d\n", *decodedInt, bufferStart[decoded], decoded);
    *decodedInt |= (bufferStart[decoded] & 0x7f) << (decoded * 7);
  } while ( bufferStart[decoded] & 0x80 );
  return decoded+1;
}

uint8_t ArduCastControl::pbDecodeHeader(uint8_t *bufferStart, uint8_t *tag, uint8_t *wire, uint32_t *lengthOrValue){
  *wire = bufferStart[0] & 0x07;
  *tag = bufferStart[0] >> 3;
  uint8_t processedBytes = 1;
  //Serial.printf("desc=0x%02x\n", bufferStart[0]);
  processedBytes += pbDecodeVarint(bufferStart+1, lengthOrValue);
  return processedBytes;
}


connection_t ArduCastControl::loop(){
  if ( !client.connected() ){
    client.stop();
    connectionStatus = DISCONNECTED;
    return DISCONNECTED;
  }
  uint32_t read;
  bool rxProcessed = false;
  
  //--------------------- RX code -----------------------------
  do {
    //download the msg to connBuffer (only accept what we expect)
    read = getRawMessage(connBuffer, CONNBUFFER_SIZE, client, 100);
    if ( read > 0){
      rxProcessed = true; //this will disable tx operations in this loop
      msgSent = false; //we assume this is a response to the message we sent
      errorCount = 5; //connection is alive, reset errorCount
      uint8_t processPayload = 0; //assume no need to process it
      //printRawMsg(read-4, connBuffer+4);
      uint32_t offset = 4; //skip the length field, it's not pb
      //iterate through protobuf
      do {
        uint8_t tag, wire;
        uint32_t lengthOrValue;
        
        offset += pbDecodeHeader(connBuffer+offset, &tag, &wire, &lengthOrValue);
        //check which device responded, accept it as pong
        if ( tag == extensions_api_cast_channel_CastMessage_source_id_tag ){
          if( 0 == memcmp(connBuffer+offset, deviceConnection.getDestinationId(), lengthOrValue))
          {
            //main device, process the payload of main RECEIVER_STATUS
            // Serial.println("Pong from device");
            deviceConnection.pinged();
            processPayload = 1;
          }
          if ( applicationConnection.getConnectionStatus() != CH_DISCONNECTED &&
              0 == memcmp(connBuffer+offset, applicationConnection.getDestinationId(), lengthOrValue))
          {
            //application, process the payload as MEDIA_STATUS
            // Serial.println("Pong from app");
            processPayload = 2;
            applicationConnection.pinged();
          }
        }
        //check the namespace, we're only process receiver and media
        if ( tag == extensions_api_cast_channel_CastMessage_namespace_fix_tag ){
          if( 0 == memcmp(connBuffer+offset, CC_NS_HEARTBEAT, lengthOrValue)){ //pong message, no need to process the payload
            processPayload = 0; 
          }
          if( 0 == memcmp(connBuffer+offset, CC_NS_CONNECTION, lengthOrValue)){ //must be a close message
            if ( processPayload == 1 ){
              applicationConnection.setDisconnect();
              deviceConnection.setDisconnect();
              connectionStatus = TCPALIVE;
              processPayload = false;
            } else if ( processPayload == 2) {
              applicationConnection.setDisconnect();
              processPayload = false;
            }
          }
        }

        if ( processPayload > 0 && tag == extensions_api_cast_channel_CastMessage_payload_utf8_tag ){
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, connBuffer+offset, lengthOrValue);
          if ( !error && doc["type"].is<const char*>() && !doc["status"].isNull() ){
            if ( processPayload == 1 && strcmp("RECEIVER_STATUS", doc["type"].as<const char*>()) == 0 ) {
              //save the generic info
              if ( doc["status"]["volume"].is<JsonObject>() ){
                if( !doc["status"]["volume"]["level"].isNull()){
                  volume = doc["status"]["volume"]["level"];
                } else
                  volume = -1.0;
                
                if( !doc["status"]["volume"]["muted"].isNull())
                  isMuted = doc["status"]["volume"]["muted"].as<bool>();
                else
                  isMuted = false;
              
              } else {
                volume = -1.0;
                isMuted = false;
              }
              if ( doc["status"]["applications"].is<JsonArray>() && !doc["status"]["applications"][0].isNull() ){
                if ( doc["status"]["applications"][0]["sessionId"].is<const char*>() ){
                  strncpy(sessionId, doc["status"]["applications"][0]["sessionId"].as<const char*>() ,sizeof(sessionId)-1);
                  sessionId[sizeof(sessionId)-1] = '\0';
                  connectionStatus = CONNECT_TO_APPLICATION;
                } else 
                  sessionId[0] = '\0';
                if ( doc["status"]["applications"][0]["statusText"].is<const char*>() ){
                  strncpy(statusText, doc["status"]["applications"][0]["statusText"].as<const char*>() ,sizeof(statusText)-1);
                  statusText[sizeof(statusText)-1] = '\0';
                } else
                  statusText[0] = '\0';
                if ( doc["status"]["applications"][0]["displayName"].is<const char*>() ){
                  strncpy(displayName, doc["status"]["applications"][0]["displayName"].as<const char*>() ,sizeof(displayName)-1);
                  displayName[sizeof(displayName)-1] = '\0';
                } else
                  displayName[0] = '\0';
              } else {
                sessionId[0] = '\0';
                statusText[0] = '\0';
                displayName[0] = '\0';
              }
            }
            if ( processPayload == 2 && strcmp("MEDIA_STATUS", doc["type"].as<const char*>()) == 0 ) {
              if ( !doc["status"][0]["mediaSessionId"].isNull() )
                mediaSessionId = doc["status"][0]["mediaSessionId"];
              else
                mediaSessionId = -1;
              
              if ( !doc["status"][0]["currentTime"].isNull() )
                currentTime = doc["status"][0]["currentTime"];
              else
                currentTime = 0.0;
            
              if ( doc["status"][0]["playerState"].is<const char*>() ){
                if ( strcmp("IDLE", doc["status"][0]["playerState"].as<const char*>()) == 0 ){
                  playerState = IDLE;
                } else if ( strcmp("BUFFERING", doc["status"][0]["playerState"].as<const char*>()) == 0 ){
                  playerState = BUFFERING;
                } else if ( strcmp("PLAYING", doc["status"][0]["playerState"].as<const char*>()) == 0 ){
                  playerState = PLAYING;
                } else if ( strcmp("PAUSED", doc["status"][0]["playerState"].as<const char*>()) == 0 ){
                  playerState = PAUSED;
                } else {
                  playerState = IDLE;
                }
              } else
                playerState = IDLE;
              
              if ( doc["status"][0]["media"].is<JsonObject>()){
                if ( !doc["status"][0]["media"]["duration"].isNull() ){
                  duration = doc["status"][0]["media"]["duration"];
                } else {
                  duration = 0.0;
                }
                if ( doc["status"][0]["media"]["metadata"].is<JsonObject>() ){
                  JsonObject meta = doc["status"][0]["media"]["metadata"].as<JsonObject>();
                  if ( meta["title"].is<const char*>() ){
                    strncpy(title, meta["title"].as<const char*>() ,sizeof(title)-1);
                    title[sizeof(title)-1] = '\0';
                  } else {
                    title[0] = '\0';
                  }
                  if ( meta["artist"].is<const char*>() ){
                    strncpy(artist, meta["artist"].as<const char*>() ,sizeof(artist)-1);
                    artist[sizeof(artist)-1] = '\0';
                  } else if ( meta["subtitle"].is<const char*>() ){
                    strncpy(artist, meta["subtitle"].as<const char*>() ,sizeof(artist)-1);
                    artist[sizeof(artist)-1] = '\0';
                  } else {
                    artist[0] = '\0';
                  }
                  if ( meta["albumName"].is<const char*>() ){
                    strncpy(album, meta["albumName"].as<const char*>() ,sizeof(album)-1);
                    album[sizeof(album)-1] = '\0';
                  } else if ( meta["seriesTitle"].is<const char*>() ){
                    strncpy(album, meta["seriesTitle"].as<const char*>() ,sizeof(album)-1);
                    album[sizeof(album)-1] = '\0';
                  } else {
                    album[0] = '\0';
                  }
                  artUrl[0] = '\0';
                  if ( meta["images"].is<JsonArray>() && meta["images"][0]["url"].is<const char*>() ){
                    strncpy(artUrl, meta["images"][0]["url"].as<const char*>(), sizeof(artUrl)-1);
                    artUrl[sizeof(artUrl)-1] = '\0';
                  }
                } else {
                  title[0] = '\0';
                  artist[0] = '\0';
                  album[0] = '\0';
                  artUrl[0] = '\0';
                }
              } else {
                //CC seems to skip sending this when it's busy, so we ignore the error
              }
            }
          }
          // serializeJsonPretty(doc, Serial);
          // Serial.println();
        }




        //DEBUG code -- this does not work when DynamicJson is used, probably not enough heap
        // Serial.printf("T:%d, W:%d", tag, wire);
        // if ( wire == 2 ){
        //   Serial.printf(" (%d): ", lengthOrValue);
        //   Serial.printf("%.*s\n", lengthOrValue, connBuffer+offset);
        // } else {
        //   Serial.printf(": %d\n", lengthOrValue);
        // }
        //end of debug code


        //for length delimited stuff, add the decoded length to the offset
        if ( wire == 2)
          offset+=lengthOrValue;
      } while(offset<read);
    }
  } while ( read > 0);
  
  // ---------------- TX code ------------------------
  //don't send msg if we just received one; wait 500ms for an answer
  if ( !rxProcessed && (!msgSent || ((millis() - msgSentAt) > 500 ))){ 
    //handle broken links
    if ( msgSent ){
      Serial.printf("EC:%d\n", errorCount);
      if (--errorCount == 0 ){
        client.stop();
        connectionStatus = DISCONNECTED;
        msgSent = false;
        return DISCONNECTED;
      }
    }

    // Serial.println("Preparing for msg");
    msgSent = false;
    int err = 0;
    if ( connectionStatus == CONNECT_TO_APPLICATION ){
      // Serial.print("CA");
      err = applicationConnection.connect(sessionId);
      if ( err == 0 )
        connectionStatus = CONNECTED;
    } else if ( applicationConnection.getConnectionStatus() == CH_DISCONNECTED ){
      // Serial.print("GS");
      err = deviceConnection.writeMsg(CC_NS_RECEIVER, CC_MSG_GET_STATUS);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    } else if ( deviceConnection.getConnectionStatus() == CH_NEEDS_PING ){
      // Serial.print("ping main");
      err = deviceConnection.writeMsg(CC_NS_HEARTBEAT, CC_MSG_PING);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    } else if ( applicationConnection.getConnectionStatus() == CH_CONNECTED ){
      // Serial.print("GSA");
      err = applicationConnection.writeMsg(CC_NS_MEDIA, CC_MSG_GET_STATUS);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    } else if ( applicationConnection.getConnectionStatus() == CH_NEEDS_PING ){ //this will never happen, unless loop is called rarely
      // Serial.print("ping app");
      err = applicationConnection.writeMsg(CC_NS_HEARTBEAT, CC_MSG_PING);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    }
    // if (msgSent ){
    //    Serial.printf("Res: %d\n", err);
    // }
  }

  return getConnection();
}

connection_t ArduCastControl::getConnection(){
  if ( msgSent )
    return WAIT_FOR_RESPONSE;
  if ( applicationConnection.getConnectionStatus() != CH_DISCONNECTED )
    return APPLICATION_RUNNING;
  
  return connectionStatus;
}

void ArduCastControl::dumpStatus(){
  if ( getConnection() != DISCONNECTED && getConnection() != TCPALIVE ){
    Serial.printf("V:%f%c\n", volume, isMuted?'M':' ');
    if ( applicationConnection.getConnectionStatus() != CH_DISCONNECTED ){
      Serial.printf("D:%s\n", displayName);
      Serial.printf("S:%s\n", statusText);
      Serial.printf("A/T:%s/%s\n", artist,  title);
      Serial.printf("S:%d %f/%f\n", playerState, duration, currentTime);
    }
  }
}



int ArduCastControl::play(){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_PLAY, mediaSessionId );
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::pause(bool toggle){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  if ( toggle && playerState == PAUSED )
    return play();
  else {
    snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_PAUSE, mediaSessionId );
    return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
  }
}

int ArduCastControl::prev(){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;

  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_PREV, mediaSessionId );
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::next(){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_NEXT, mediaSessionId );
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::seek(bool relative, float seekTo){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  if ( relative )
    seekTo += currentTime;
  
  if ( seekTo < 0 )
    seekTo = 0;
  if ( seekTo > duration )
    seekTo = duration;

  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "{\"type\": \"SEEK\", \"requestId\": 2, \"mediaSessionId\": %d, \"currentTime\": %f}", mediaSessionId, seekTo);
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::setVolume(bool relative, float volumeTo){
  if ( msgSent )
    return -10;

  if ( relative )
    volumeTo += volume;
  
  if ( volumeTo < 0 )
    volumeTo = 0;
  if ( volumeTo > 1 )
    volumeTo = 1;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%f}}", CC_MSG_SET_VOL, volumeTo );
  return deviceConnection.writeMsg(CC_NS_RECEIVER, cc_msg_ctrl);
}

int ArduCastControl::setMute(bool newMute, bool toggle){
  if ( msgSent )
    return -10;

  if ( toggle )
    newMute = !isMuted;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%s}}", CC_MSG_VOL_MUTE, newMute?"true":"false" );
  return deviceConnection.writeMsg(CC_NS_RECEIVER, cc_msg_ctrl);

}
