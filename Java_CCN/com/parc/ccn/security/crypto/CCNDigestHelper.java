package com.parc.ccn.security.crypto;

import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateEncodingException;

import javax.xml.stream.XMLStreamException;

import com.parc.ccn.Library;
import com.parc.ccn.data.util.XMLEncodable;
import com.parc.security.crypto.DigestHelper;

public class CCNDigestHelper extends DigestHelper {

	public static String DEFAULT_DIGEST_ALGORITHM = "SHA-256";
	// public static String DEFAULT_DIGEST_ALGORITHM = "SHA-1";
	public static int DEFAULT_DIGEST_LENGTH = 64;
	
	public CCNDigestHelper() {
		super();
		if (!_md.getAlgorithm().equals(CCNDigestHelper.DEFAULT_DIGEST_ALGORITHM)) {
			Library.logger().severe("Incorrect constructor in CCNDigestHelper -- picking up wrong digest algorithm!");
		}
	}
		
	/**
	 * Same digest preparation algorithm as ContentObject.
	 * @throws XMLStreamException 
	 * @throws NoSuchAlgorithmException 
	 */
	public static byte [] digestLeaf(
			String digestAlgorithm,
			XMLEncodable [] toBeSigneds,
			byte [][] additionalToBeSigneds) throws XMLStreamException, NoSuchAlgorithmException {
		
		if (null == toBeSigneds) {
			Library.logger().info("Value to be signed must not be null.");
			throw new XMLStreamException("Unexpected null content in digestLeaf!");
		}
		byte [][] encodedData = new byte [toBeSigneds.length + ((null != additionalToBeSigneds) ? additionalToBeSigneds.length : 0)][];
		for (int i=0; i < toBeSigneds.length; ++i) {
			encodedData[i] = toBeSigneds[i].encode();
		}
		if (null != additionalToBeSigneds) {
			for (int i=0,j=toBeSigneds.length; j < encodedData.length; ++i,++j) {
				encodedData[j] = additionalToBeSigneds[i];
			}
		}
		return DigestHelper.digest(((null == digestAlgorithm) || (digestAlgorithm.length() == 0)) ?
				CCNDigestHelper.DEFAULT_DIGEST_ALGORITHM : digestAlgorithm, 
									encodedData);
	}

	@Override
	public String getDefaultDigest() { return DEFAULT_DIGEST_ALGORITHM; }
	
	public static byte [] digest(byte [] content, int offset, int length) {
		CCNDigestHelper dh = new CCNDigestHelper();
		dh.update(content, offset, length);
		return dh.digest();
	}
	
	public static byte [] digest(byte [][] contents) {
		CCNDigestHelper dh = new CCNDigestHelper();
		for (int i=0; i < contents.length; ++i) {
			dh.update(contents[i], 0, contents[i].length);
		}
		return dh.digest();
	}	

	public static byte [] encodedDigest(byte [] content) throws CertificateEncodingException {
		byte [] digest = digest(content);
		return digestEncoder(CCNDigestHelper.DEFAULT_DIGEST_ALGORITHM, digest);
	}


}
